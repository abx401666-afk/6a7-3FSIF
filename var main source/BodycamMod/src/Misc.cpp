#include "../include/Misc.hpp"
#include "../include/Memory.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Log.hpp"
#include "../include/Targets.hpp"
#include "../include/ESP.hpp"
#include "../include/World.hpp"
#include <algorithm>
#include <cstring>
#include <atomic>
#include <thread>
#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

MiscSettings g_Misc;

namespace {
    const int64_t& kWeaponSet    = OFF(WeaponSet);
    const int64_t& kBaseValue    = OFF(AttrBaseValue);
    const int64_t& kCurrentValue = OFF(AttrCurrentValue);
    const int64_t& kTimeDilation = OFF(ActorTimeDilation);

    const int64_t* const kRecoil[] = { &OFF(WaCameraRecoilStep), &OFF(WaVerticalRecoil), &OFF(WaHorizontalRecoil), &OFF(WaCameraRecoilStepVertical),
        &OFF(WaCameraRecoilStepHorizontal), &OFF(WaKickUp), &OFF(WaKickHip), &OFF(WaMagnifiedKickHip), &OFF(WaMagnifiedKickUp),
        &OFF(WaMagnifiedMaxKickHip), &OFF(WaMagnifiedMaxKickUp) };
    const int64_t* const kSpread[] = { &OFF(WaSpreadX), &OFF(WaSpreadY), &OFF(WaSpreadAimX), &OFF(WaSpreadAimY), &OFF(WaSpreadHipX), &OFF(WaSpreadHipY) };
    const int64_t& kFireRate     = OFF(WaFireRate);
    const int64_t& kAmmoCapacity = OFF(WaAmmoCapacity);
    const int64_t& kCurrentAmmo  = OFF(WaCurrentAmmo);
    const int64_t& kDamage       = OFF(WaDamage);
    const int64_t& kWepSpeed     = OFF(WaSpeed);
    const int64_t& kReloadTime   = OFF(WaReloadTime);

    const int64_t& kStHealth     = OFF(SurvivorStatusHealth);
    const int64_t& kStHealthMax  = OFF(SurvivorStatusMaxHealth);
    const int64_t& kStInvincible = OFF(SurvivorStatusInvincible);

    const int64_t& kCharMove     = OFF(CharacterMovement);
    const int64_t& kMoveMode     = OFF(MoveMode);
    const int64_t& kBrakeFlying  = OFF(MoveBrakingFlying);
    const int64_t& kCheatFlying  = OFF(MoveCheatFlyingByte);
    const int64_t& kVelocity     = OFF(MoveVelocity);
    const int64_t& kActorColl    = OFF(ActorCollisionByte);
    const int64_t& kRoot         = OFF(ActorRootComponent);
    const int64_t& kRelLoc       = OFF(SceneRelativeLocation);
    constexpr uint8_t   MOVE_Falling  = 3, MOVE_Flying = 5;

    HANDLE g_wh = nullptr;
    DWORD  g_whPid = 0;
    bool   g_writeFailed = false;

    struct Slot { float orig = 0.0f; float written = 0.0f; };
    uintptr_t g_set = 0, g_checkedSet = 0;
    bool g_setOk = false;
    std::map<ptrdiff_t, Slot> g_slots;

    uintptr_t g_speedPawn = 0;
    std::set<uintptr_t> g_frozen;
    int g_ammoRefills = 0;

    bool Plausible(uintptr_t p) { return p > 0x10000 && p < 0x7FFFFFFFFFFFull && (p & 7) == 0; }

    bool EnsureHandle() {
        const DWORD pid = GetAttachedProcessId();
        if (!pid) return false;
        if (g_wh && g_whPid == pid) return true;
        if (g_wh) CloseHandle(g_wh);
        g_wh = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
        g_whPid = pid;
        if (!g_wh) {
            if (!g_writeFailed) Log_Add(Log_Warn, "Misc: could not open the game for writing (run as admin?)");
            g_writeFailed = true;
            return false;
        }
        g_writeFailed = false;
        g_set = g_checkedSet = 0; g_slots.clear(); g_speedPawn = 0; g_frozen.clear();
        return true;
    }

    bool WriteF(uintptr_t addr, float v) {
        SIZE_T n = 0;
        return g_wh && WriteProcessMemory(g_wh, reinterpret_cast<LPVOID>(addr), &v, sizeof(v), &n) && n == sizeof(v);
    }
    bool ReadF(uintptr_t addr, float& v) { return ReadRemote(addr, v) && std::isfinite(v); }
    template <class T> bool WriteT(uintptr_t addr, const T& v) {
        SIZE_T n = 0;
        return g_wh && WriteProcessMemory(g_wh, reinterpret_cast<LPVOID>(addr), &v, sizeof(T), &n) && n == sizeof(T);
    }
    bool SetBit(uintptr_t addr, uint8_t mask, bool on) {
        uint8_t b = 0;
        if (!ReadRemote(addr, b)) return false;
        const uint8_t nb = on ? (b | mask) : (b & ~mask);
        return nb == b || WriteT(addr, nb);
    }
    bool Finite(const FVector& v) { return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z); }

    bool WriteAttr(uintptr_t set, ptrdiff_t f, float v) {
        const bool a = WriteF(set + f + kBaseValue, v);
        const bool b = WriteF(set + f + kCurrentValue, v);
        return a && b;
    }

    template <class Fn>
    void Hold(ptrdiff_t f, Fn target) {
        float cur = 0.0f;
        if (!ReadF(g_set + f + kCurrentValue, cur)) return;
        auto it = g_slots.find(f);
        if (it == g_slots.end() || std::fabs(cur - it->second.written) > 1e-4f) {
            Slot s; s.orig = cur;
            it = g_slots.insert_or_assign(f, s).first;
        }
        const float t = target(it->second.orig);
        it->second.written = t;
        if (std::fabs(cur - t) > 1e-4f) WriteAttr(g_set, f, t);
    }

    void Release(ptrdiff_t f) {
        auto it = g_slots.find(f);
        if (it == g_slots.end()) return;
        float cur = 0.0f;
        if (g_set && IsObjectAlive(g_set) && ReadF(g_set + f + kCurrentValue, cur) && std::fabs(cur - it->second.written) <= 1e-4f)
            WriteAttr(g_set, f, it->second.orig);
        g_slots.erase(it);
    }

    float FasterRate(float orig, float mul) {
        if (mul < 1.0f) mul = 1.0f;
        return orig > 20.0f ? orig * mul : orig / mul;
    }

    void UpdateWeapon(uintptr_t pawn) {
        uintptr_t set = 0;
        if (!pawn || !ReadRemote(pawn + kWeaponSet, set) || !Plausible(set)) set = 0;
        if (set && set != g_checkedSet) {
            g_checkedSet = set;
            g_setOk = false;
            for (const std::string& n : Skeleton_ClassChain(set))
                if (n.find("weaponattributeset") != std::string::npos) { g_setOk = true; break; }
        }
        if (set != g_set) {
            g_slots.clear();

            g_set = set;
        }
        if (!g_set || !g_setOk) return;

        const HeldItem* held = World_HeldBy(pawn);
        const bool gun = held && held->kind >= Weapon_Rifle && held->kind <= Weapon_Sniper;
        if (!gun) {
            g_slots.clear();

            return;
        }

        for (const int64_t* fp : kRecoil) { const ptrdiff_t f = *fp; if (g_Misc.NoRecoil) Hold(f, [](float) { return 0.0f; }); else Release(f); }
        for (const int64_t* fp : kSpread) { const ptrdiff_t f = *fp; if (g_Misc.NoSpread) Hold(f, [](float) { return 0.0f; }); else Release(f); }

        if (g_Misc.RapidFire) {
            Hold(kFireRate, [](float o) { return o > 0.0f ? FasterRate(o, g_Misc.FireRateMul) : o; });
        } else {
            Release(kFireRate);
        }

        if (g_Misc.InstaReload) Hold(kReloadTime, [](float o) {
            const float m = g_Misc.ReloadSpeedMul < 1.0f ? 1.0f : g_Misc.ReloadSpeedMul;
            return o > 0.0f ? o * m : m; }); else Release(kReloadTime);
        if (g_Misc.DamageBoost) Hold(kDamage, [](float o) { return o * (g_Misc.DamageMul < 1.0f ? 1.0f : g_Misc.DamageMul); }); else Release(kDamage);
        if (g_Misc.WeaponSpeed) Hold(kWepSpeed, [](float o) { return o * (g_Misc.WeaponSpeedMul < 1.0f ? 1.0f : g_Misc.WeaponSpeedMul); }); else Release(kWepSpeed);

        if (g_Misc.InfiniteAmmo) {
            float cur = 0.0f, cap = 0.0f;
            if (ReadF(g_set + kCurrentAmmo + kCurrentValue, cur) && ReadF(g_set + kAmmoCapacity + kCurrentValue, cap)
                && cap > 0.0f && cap < 1000.0f && cur < cap) {
                if (WriteAttr(g_set, kCurrentAmmo, cap)) ++g_ammoRefills;
            }
        }
    }

    void SetDilation(uintptr_t actor, float v) {
        if (!IsObjectAlive(actor)) return;
        float cur = 0.0f;
        if (ReadF(actor + kTimeDilation, cur) && std::fabs(cur - v) > 1e-4f) WriteF(actor + kTimeDilation, v);
    }

    void UpdateTime(uintptr_t local) {
        if (g_Misc.Speedhack && local) {
            if (g_speedPawn && g_speedPawn != local) SetDilation(g_speedPawn, 1.0f);
            g_speedPawn = local;
            float m = g_Misc.SpeedMul;
            if (m < 0.1f) m = 0.1f;
            if (m > 10.0f) m = 10.0f;
            SetDilation(local, m);
        } else if (g_speedPawn) {
            SetDilation(g_speedPawn, 1.0f);
            g_speedPawn = 0;
        }

        std::set<uintptr_t> roster;
        if (g_Misc.FreezeBots || !g_frozen.empty())
            for (const PawnEntry& e : GetCharacterPawns())
                if (e.pawn && e.pawn != local && IsBotPawn(e.pawn)) roster.insert(e.pawn);

        float s = g_Misc.BotTimeScale;
        if (s < 0.0f) s = 0.0f;
        if (s > 1.0f) s = 1.0f;
        std::set<uintptr_t> want;
        for (uintptr_t p : roster) {
            if (g_Misc.FreezeBots) { SetDilation(p, s); want.insert(p); }
        }
        for (uintptr_t p : g_frozen)
            if (!want.count(p) && roster.count(p)) SetDilation(p, 1.0f);
        g_frozen = want;

    }

    const int64_t& kSurvivorAttrSet = OFF(SurvivorAttrSet);
    const int64_t& kPawnCharSet     = OFF(PawnCharacterSet);
    const int64_t& kAttrHealth      = OFF(AttrHealth);
    const int64_t& kAttrMaxHealth   = OFF(AttrMaxHealth);
    constexpr float     kGodHealth       = 100000.0f;

    uintptr_t g_godStatus = 0, g_godSet = 0;
    uint8_t   g_godOrigInv = 0;
    float     g_godOrigHp = -1.0f, g_godOrigMax = -1.0f;

    uintptr_t CharSet(uintptr_t local, uintptr_t st) {
        uintptr_t set = 0;
        if (st && ReadRemote(st + kSurvivorAttrSet, set) && Plausible(set)) return set;
        if (local && ReadRemote(local + kPawnCharSet, set) && Plausible(set)) return set;
        return 0;
    }

    void GodRelease() {
        if (g_godStatus && !IsObjectAlive(g_godStatus)) g_godStatus = 0;
        if (g_godSet && !IsObjectAlive(g_godSet)) g_godSet = 0;
        if (g_godStatus) WriteT(g_godStatus + kStInvincible, g_godOrigInv);
        if (g_godSet && g_godOrigMax > 0.0f) {
            WriteAttr(g_godSet, kAttrMaxHealth, g_godOrigMax);
            float hp = g_godOrigHp > 0.0f ? g_godOrigHp : g_godOrigMax;
            if (hp > g_godOrigMax) hp = g_godOrigMax;
            WriteAttr(g_godSet, kAttrHealth, hp);
        }
        g_godStatus = g_godSet = 0;
        g_godOrigHp = g_godOrigMax = -1.0f;
    }

    void UpdateGod(uintptr_t local) {
        const uintptr_t st = local ? GetSurvivorStatus(local) : 0;
        const uintptr_t set = CharSet(local, st);
        if ((g_godStatus || g_godSet) && (!g_Misc.GodMode || st != g_godStatus || set != g_godSet)) GodRelease();
        if (!g_Misc.GodMode || !local) { return; }

        if (Plausible(st)) {
            if (!g_godStatus) {
                g_godStatus = st;
                ReadRemote(st + kStInvincible, g_godOrigInv);
                if (g_godOrigInv > 1) g_godOrigInv = 0;
            }
            WriteT<uint8_t>(st + kStInvincible, 1);
            double hp = 0.0, mx = 0.0;
            if (ReadRemote(st + kStHealth, hp) && ReadRemote(st + kStHealthMax, mx) &&
                std::isfinite(hp) && std::isfinite(mx) && mx > 0.0 && mx < 1e7 && hp < mx)
                WriteT(st + kStHealth, mx);
        }

        if (set) {
            float hp = 0.0f, mx = 0.0f;
            if (ReadF(set + kAttrHealth + kCurrentValue, hp) && ReadF(set + kAttrMaxHealth + kCurrentValue, mx)) {
                if (!g_godSet) {
                    g_godSet = set;
                    g_godOrigHp = hp;
                    g_godOrigMax = (mx > 0.0f && mx < kGodHealth * 0.5f) ? mx : 100.0f;
                }
                if (mx < kGodHealth) WriteAttr(set, kAttrMaxHealth, kGodHealth);
                if (hp < kGodHealth) WriteAttr(set, kAttrHealth, kGodHealth);
            }
        }
    }

    ptrdiff_t g_c2w = -1;

    ptrdiff_t FindC2W(uintptr_t root) {
        if (g_c2w >= 0) return g_c2w;
        FVector rel{};
        if (!ReadRemote(root + kRelLoc, rel) || !Finite(rel) || rel.LengthSq() < 1.0) return -1;
        alignas(16) unsigned char buf[0x300];
        if (!ReadRemoteBytes(root, buf, sizeof(buf))) return -1;
        for (ptrdiff_t off = 0x140; off + 0x60 <= static_cast<ptrdiff_t>(sizeof(buf)); off += 0x10) {
            double q[4], t[3], sc[3];
            std::memcpy(q, buf + off, sizeof(q));
            std::memcpy(t, buf + off + 0x20, sizeof(t));
            std::memcpy(sc, buf + off + 0x40, sizeof(sc));
            const double qn = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
            if (!std::isfinite(qn) || std::fabs(qn - 1.0) > 0.01) continue;
            if (std::fabs(t[0] - rel.X) > 2.0 || std::fabs(t[1] - rel.Y) > 2.0 || std::fabs(t[2] - rel.Z) > 2.0) continue;
            if (!(sc[0] > 0.01 && sc[0] < 100.0 && sc[1] > 0.01 && sc[1] < 100.0 && sc[2] > 0.01 && sc[2] < 100.0)) continue;
            g_c2w = off;
            char b[96]; std::snprintf(b, sizeof(b), "Misc: ComponentToWorld at root+0x%X", static_cast<unsigned>(off));
            Log_Add(Log_Info, "%s", b);
            return off;
        }
        return -1;
    }

    bool C2WMatches(uintptr_t root, ptrdiff_t off, const FVector& rel) {
        double q[4], t[3];
        if (!ReadRemote(root + off, q) || !ReadRemote(root + off + 0x20, t)) return false;
        const double qn = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        return std::isfinite(qn) && std::fabs(qn - 1.0) < 0.01 &&
               std::fabs(t[0] - rel.X) < 2.0 && std::fabs(t[1] - rel.Y) < 2.0 && std::fabs(t[2] - rel.Z) < 2.0;
    }

    bool MoveActorTo(uintptr_t pawn, const FVector& dst) {
        if (!Finite(dst) || std::fabs(dst.X) > 1.0e6 || std::fabs(dst.Y) > 1.0e6 || std::fabs(dst.Z) > 1.0e6) {
            Log_Add(Log_Error, "Misc: teleport skipped, bad destination");
            return false;
        }
        uintptr_t root = 0;
        if (!ReadRemote(pawn + kRoot, root) || !Plausible(root)) return false;
        FVector rel{};
        if (!ReadRemote(root + kRelLoc, rel) || !Finite(rel)) return false;
        const FVector d{ dst.X - rel.X, dst.Y - rel.Y, dst.Z - rel.Z };
        if (d.LengthSq() > 2.0e5 * 2.0e5) { Log_Add(Log_Error, "Misc: teleport skipped, target over 2 km away"); return false; }
        ptrdiff_t c2w = FindC2W(root);
        if (c2w >= 0 && !C2WMatches(root, c2w, rel)) { g_c2w = -1; c2w = FindC2W(root); }
        if (c2w < 0 || !C2WMatches(root, c2w, rel)) { Log_Add(Log_Error, "Misc: teleport skipped, transform not found"); return false; }
        Log_Add(Log_Info, "Misc: teleport %.0f %.0f %.0f -> %.0f %.0f %.0f", rel.X, rel.Y, rel.Z, dst.X, dst.Y, dst.Z);
        bool ok = WriteT(root + kRelLoc, dst);
        ok = WriteT(root + c2w + 0x20, dst) && ok;
        uintptr_t mv = 0;
        if (ReadRemote(pawn + kCharMove, mv) && Plausible(mv)) WriteT(mv + kVelocity, FVector{});
        return ok;
    }

    bool KeyDown(int vk) { return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0; }

    bool g_tpWasDown = false;
    uintptr_t g_tpLastPawn = 0;

    bool Valid(const Target& t) { return !t.drone && !t.friendly && !t.stale && t.health != 0.0; }

    const Target* Pick(const TargetFrame& f, int mode) {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        const Target* best = nullptr;
        float bestScore = 1e30f;
        for (const Target& t : f.targets) {
            if (!Valid(t)) continue;
            float score = t.distM;
            if (mode == 0) { ImVec2 sp; score = t.distM * 1000.0f; if (ESP_Project(t.pos, sp)) score = std::hypot(sp.x - c.x, sp.y - c.y); }
            if (score < bestScore) { bestScore = score; best = &t; }
        }
        return best;
    }

    FVector Spot(const Target& t) {
        const double yaw = std::isfinite(t.yaw) ? DegToRad(t.yaw) : 0.0;
        const double d = std::clamp(static_cast<double>(g_Misc.TeleportDist), 50.0, 600.0);
        const FVector fw{ std::cos(yaw), std::sin(yaw), 0.0 };
        FVector p = t.pos;
        if (g_Misc.TeleportSide == 1) p = p + fw * d;
        else if (g_Misc.TeleportSide == 2) p.Z += (std::isfinite(t.halfHeight) && t.halfHeight > 0.0 && t.halfHeight < 300.0 ? t.halfHeight : 90.0) * 2.0 + d;
        else p = p - fw * d;
        p.Z += 5.0;
        return p;
    }

    double g_tpLastJump = -1.0;
    bool Jump(uintptr_t local, const Target& t) {
        if (t.pawn == local) return false;
        const double now = ImGui::GetTime();
        if (g_tpLastJump >= 0.0 && now - g_tpLastJump < 0.25) return false;
        g_tpLastJump = now;
        const bool ok = MoveActorTo(local, Spot(t));
        g_tpLastPawn = t.pawn;
        Log_Add(Log_Info, ok ? "Misc: teleported to %s" : "Misc: teleport to %s failed", t.name.c_str());
        return ok;
    }

    void UpdateTeleport(uintptr_t local, bool fg) {
        const TargetFrame& f = Targets_Get();
        if (!local) return;

        static bool wasOn = false;
        if (!g_Misc.Teleport) { g_tpWasDown = false; wasOn = false; return; }
        if (!wasOn) {
            wasOn = true;
            g_tpWasDown = true;
            g_tpLastPawn = 0;
            Log_Add(Log_Info, "Misc: teleport armed");
            return;
        }

        const bool down = fg && KeyDown(g_Misc.TeleportKey);
        const bool pressed = down && !g_tpWasDown;
        g_tpWasDown = down;
        if (pressed)
            if (const Target* t = Pick(f, 0)) Jump(local, *t);

        if (g_Misc.AutoTeleport && g_tpLastPawn) {
            bool alive = false;
            for (const Target& t : f.targets) if (t.pawn == g_tpLastPawn && t.health != 0.0) alive = true;
            if (!alive) {
                const Target* t = Pick(f, 2);
                if (t) Jump(local, *t);
                else g_tpLastPawn = 0;
            }
        }
    }

    uintptr_t g_flyPawn = 0, g_flyMove = 0;
    float g_origBrake = 0.0f;
    bool  g_collOff = false;

    std::atomic<bool>      g_ftRun{ false };
    std::atomic<uintptr_t> g_ftRoot{ 0 }, g_ftMove{ 0 };
    std::atomic<long long> g_ftC2W{ -1 };
    std::atomic<double>    g_ftPitch{ 0.0 }, g_ftYaw{ 0.0 };
    std::atomic<bool>      g_ftFg{ false };
    std::atomic<float>     g_ftSpeed{ 1200.0f }, g_ftResp{ 6.0f };
    std::atomic<int>       g_ftUp{ 0x20 }, g_ftDown{ 0x11 }, g_ftBoost{ 0x10 };
    std::thread            g_ftThread;

    void FlyThread() {
        LARGE_INTEGER freq, last, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&last);
        FVector vel{};
        while (g_ftRun) {
            Sleep(1);
            QueryPerformanceCounter(&now);
            double dt = static_cast<double>(now.QuadPart - last.QuadPart) / freq.QuadPart;
            last = now;
            if (dt <= 0.0 || dt > 0.05) dt = 0.002;
            const uintptr_t root = g_ftRoot, mv = g_ftMove;
            const long long c2w = g_ftC2W;
            if (!root || c2w < 0) { vel = {}; continue; }
            if (!IsObjectAlive(root)) { vel = {}; g_ftRoot = 0; continue; }

            FVector target{};
            if (g_ftFg) {
                const double p = DegToRad(g_ftPitch), y = DegToRad(g_ftYaw);
                const FVector fwd{ std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p) };
                const FVector right{ -std::sin(y), std::cos(y), 0.0 };
                FVector dir{};
                if (KeyDown('W')) dir = dir + fwd;
                if (KeyDown('S')) dir = dir - fwd;
                if (KeyDown('D')) dir = dir + right;
                if (KeyDown('A')) dir = dir - right;
                if (KeyDown(g_ftUp)) dir.Z += 1.0;
                if (KeyDown(g_ftDown)) dir.Z -= 1.0;
                const double len = dir.Length();
                if (len > 1e-6) target = dir * (g_ftSpeed * (KeyDown(g_ftBoost) ? 2.0 : 1.0) / len);
            }
            const double resp = g_ftResp;
            const double k = 1.0 - std::exp(-dt * (target.LengthSq() > 0.0 ? resp : resp * 1.6));
            vel = vel + (target - vel) * k;
            if (target.LengthSq() == 0.0 && vel.LengthSq() < 1.0) vel = {};

            if (mv) WriteT(mv + kVelocity, FVector{});
            if (vel.LengthSq() == 0.0) continue;
            FVector pos{};
            if (!ReadRemote(root + kRelLoc, pos) || !Finite(pos)) continue;
            pos = pos + vel * dt;
            WriteT(root + kRelLoc, pos);
            WriteT(root + static_cast<ptrdiff_t>(c2w) + 0x20, pos);
        }
    }

    constexpr double kLandLift = 110.0;
    void StopFly() {
        g_ftRun = false;
        if (g_ftThread.joinable()) g_ftThread.join();
        g_ftRoot = 0; g_ftMove = 0;
        if (g_flyPawn && !IsObjectAlive(g_flyPawn)) g_flyPawn = 0;
        if (g_flyMove && !IsObjectAlive(g_flyMove)) g_flyMove = 0;
        if (g_collOff && g_flyPawn) SetBit(g_flyPawn + kActorColl, static_cast<uint8_t>(1u << OFF(ActorCollisionBit)), true);
        if (g_flyPawn) {
            uintptr_t root = 0; FVector pos{};
            if (ReadRemote(g_flyPawn + kRoot, root) && Plausible(root) && ReadRemote(root + kRelLoc, pos) && Finite(pos)) {
                pos.Z += kLandLift;
                MoveActorTo(g_flyPawn, pos);
            }
        }
        if (g_flyMove) {
            WriteT(g_flyMove + kVelocity, FVector{});
            WriteT(g_flyMove + kBrakeFlying, g_origBrake);
            SetBit(g_flyMove + kCheatFlying, static_cast<uint8_t>(1u << OFF(MoveCheatFlyingBit)), false);
            uint8_t mode = 0;
            if (ReadRemote(g_flyMove + kMoveMode, mode) && mode == MOVE_Flying) WriteT(g_flyMove + kMoveMode, MOVE_Falling);
        }
        g_flyPawn = g_flyMove = 0;
        g_collOff = false;
    }

    void UpdateFly(uintptr_t local, bool fg) {
        const bool want = (g_Misc.Fly || g_Misc.Noclip) && local;
        if (g_flyPawn && (!want || g_flyPawn != local)) StopFly();
        if (!want) return;
        uintptr_t mv = 0, root = 0;
        if (!ReadRemote(local + kCharMove, mv) || !Plausible(mv)) return;
        if (!ReadRemote(local + kRoot, root) || !Plausible(root)) return;
        const ptrdiff_t c2w = FindC2W(root);
        if (!g_flyPawn) {
            g_flyPawn = local; g_flyMove = mv;
            if (!ReadF(mv + kBrakeFlying, g_origBrake)) g_origBrake = 0.0f;
        }
        uint8_t mode = 0;
        if (ReadRemote(mv + kMoveMode, mode) && mode != MOVE_Flying) WriteT(mv + kMoveMode, MOVE_Flying);
        SetBit(mv + kCheatFlying, static_cast<uint8_t>(1u << OFF(MoveCheatFlyingBit)), true);
        float cur = 0.0f;
        if (ReadF(mv + kBrakeFlying, cur) && cur < 7999.0f) WriteT(mv + kBrakeFlying, 8000.0f);
        if (g_Misc.Noclip != g_collOff) {
            SetBit(local + kActorColl, static_cast<uint8_t>(1u << OFF(ActorCollisionBit)), !g_Misc.Noclip);
            g_collOff = g_Misc.Noclip;
        }

        const TargetFrame& f = Targets_Get();
        if (f.camValid) { g_ftPitch = f.cam.Rotation.Pitch; g_ftYaw = f.cam.Rotation.Yaw; }
        g_ftFg = fg && f.camValid;
        float sp = g_Misc.FlySpeed; sp = sp < 100.0f ? 100.0f : sp > 10000.0f ? 10000.0f : sp;
        float rs = g_Misc.FlyResponse; rs = rs < 0.5f ? 0.5f : rs > 20.0f ? 20.0f : rs;
        g_ftSpeed = sp; g_ftResp = rs;
        g_ftUp = g_Misc.FlyUpKey; g_ftDown = g_Misc.FlyDownKey; g_ftBoost = g_Misc.FlyBoostKey;
        g_ftMove = mv; g_ftRoot = root; g_ftC2W = c2w;
        if (!g_ftThread.joinable()) { g_ftRun = true; g_ftThread = std::thread(FlyThread); }
    }

    std::atomic<bool>  g_afWanted{ false };
    std::atomic<bool>  g_afPhys{ false };
    std::atomic<float> g_afCps{ 12.0f };
    std::atomic<DWORD> g_afThreadId{ 0 };
    std::atomic<int>   g_afClicks{ 0 };
    std::thread        g_afThread;

    LRESULT CALLBACK AfHook(int code, WPARAM wp, LPARAM lp) {
        if (code == HC_ACTION) {
            const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
            if (!(m->flags & LLMHF_INJECTED)) {
                if (wp == WM_LBUTTONDOWN) g_afPhys = true;
                else if (wp == WM_LBUTTONUP) g_afPhys = false;
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    void SendLeft(bool down) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        SendInput(1, &in, sizeof(in));
    }

    void AfThread() {
        g_afThreadId = GetCurrentThreadId();
        HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, AfHook, GetModuleHandleW(nullptr), 0);
        if (!hook) Log_Add(Log_Warn, "Misc: auto fire mouse hook failed");
        LARGE_INTEGER freq, now;
        QueryPerformanceFrequency(&freq);
        bool injectedUp = false;
        double phase = 0.0;
        bool running = true;
        while (running) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            QueryPerformanceCounter(&now);
            const double t = static_cast<double>(now.QuadPart) / freq.QuadPart;
            float cps = g_afCps;
            if (cps < 2.0f) cps = 2.0f;
            if (cps > 30.0f) cps = 30.0f;
            constexpr double kMinHalf = 0.034;
            const double period = (std::max)(1.0 / cps, kMinHalf * 2.0);
            const double downFor = (std::max)(period * 0.5, kMinHalf);
            if (g_afPhys && g_afWanted && hook) {
                if (phase == 0.0) { phase = t; injectedUp = false; }
                const double k = std::fmod(t - phase, period);
                if (!injectedUp && k >= downFor) { SendLeft(false); injectedUp = true; }
                else if (injectedUp && k < downFor) { SendLeft(true); injectedUp = false; ++g_afClicks; }
            } else {
                if (injectedUp && g_afPhys) { SendLeft(true); }
                phase = 0.0; injectedUp = false;
            }
        }
        if (hook) UnhookWindowsHookEx(hook);
        g_afThreadId = 0;
    }

    void StopAutoFire();

    void UpdateAutoFire(bool fg) {
        g_afCps = g_Misc.AutoFireCps;
        g_afWanted = g_Misc.AutoFire && fg;
        if (g_Misc.AutoFire && !g_afThread.joinable()) g_afThread = std::thread(AfThread);
        if (!g_Misc.AutoFire && g_afThread.joinable()) StopAutoFire();
    }

    void StopAutoFire() {
        g_afWanted = false;
        if (!g_afThread.joinable()) return;
        for (int i = 0; i < 100 && !g_afThreadId; ++i) Sleep(5);
        if (g_afThreadId) PostThreadMessageW(g_afThreadId, WM_QUIT, 0, 0);
        g_afThread.join();
    }

    const int64_t& kRecoilComp   = OFF(RecoilComponent);
    const int64_t& kBafCamAnim   = OFF(BafCameraAnimation);
    const int64_t& kRcData       = OFF(RcRecoilData);
    const int64_t& kRcTransMul   = OFF(RcTranslationMul);
    const int64_t& kRcRotMul     = OFF(RcRotationMul);
    const int64_t& kRdCtrlRecoil = OFF(RdControllerRecoil);
    const int64_t& kRdFireSway   = OFF(RdFireSway);
    const int64_t& kBafScale     = OFF(BafAnimationScale);

    struct RcComp { uintptr_t p = 0; FVector trans{}, rot{}; bool ok = false; };
    struct RdOrig { double ctrl[4]; float sway[2]; };
    RcComp g_rc;
    std::map<uintptr_t, RdOrig> g_rdOrig;
    uintptr_t g_baf = 0; float g_bafOrig = 1.0f;

    bool IsClass(uintptr_t obj, const char* needle) {
        for (const std::string& n : Skeleton_ClassChain(obj))
            if (n.find(needle) != std::string::npos) return true;
        return false;
    }

    void RecoilRelease() {
        if (g_rc.p && !IsObjectAlive(g_rc.p)) g_rc = {};
        if (g_baf && !IsObjectAlive(g_baf)) g_baf = 0;
        if (g_rc.p && g_rc.ok) { WriteT(g_rc.p + kRcTransMul, g_rc.trans); WriteT(g_rc.p + kRcRotMul, g_rc.rot); }
        g_rc = {};
        for (const auto& [rd, o] : g_rdOrig) {
            if (!IsObjectAlive(rd)) continue;
            WriteT(rd + kRdCtrlRecoil, o.ctrl);
            WriteT(rd + kRdFireSway, o.sway);
        }
        g_rdOrig.clear();
        if (g_baf) WriteT(g_baf + kBafScale, g_bafOrig);
        g_baf = 0;
    }

    void UpdateRecoilAnim(uintptr_t local) {
        if (!g_Misc.NoRecoil || !local) { if (g_rc.p || !g_rdOrig.empty() || g_baf) RecoilRelease(); return; }
        uintptr_t comp = 0;
        if (!ReadRemote(local + kRecoilComp, comp) || !Plausible(comp)) { return; }
        if (comp != g_rc.p) {
            if (g_rc.p) RecoilRelease();
            if (!IsClass(comp, "recoilanimation")) { return; }
            g_rc.p = comp;
            g_rc.ok = ReadRemote(comp + kRcTransMul, g_rc.trans) && ReadRemote(comp + kRcRotMul, g_rc.rot)
                      && Finite(g_rc.trans) && Finite(g_rc.rot);
        }
        if (g_rc.ok) {
            FVector cur{};
            if (ReadRemote(comp + kRcRotMul, cur) && cur.LengthSq() > 0.0) WriteT(comp + kRcRotMul, FVector{});
            if (ReadRemote(comp + kRcTransMul, cur) && cur.LengthSq() > 0.0) WriteT(comp + kRcTransMul, FVector{});
        }
        uintptr_t rd = 0;
        if (ReadRemote(comp + kRcData, rd) && Plausible(rd)) {
            if (!g_rdOrig.count(rd)) {
                RdOrig o{};
                if (ReadRemoteBytes(rd + kRdCtrlRecoil, o.ctrl, sizeof(o.ctrl)) && ReadRemoteBytes(rd + kRdFireSway, o.sway, sizeof(o.sway))
                    && std::isfinite(o.ctrl[0]) && std::fabs(o.ctrl[0]) < 1000.0 && std::isfinite(o.sway[0]))
                    g_rdOrig[rd] = o;
            }
            if (g_rdOrig.count(rd)) {
                const double z4[4] = { 0, 0, 0, 0 };
                const float z2[2] = { 0, 0 };
                double cur[4];
                if (ReadRemoteBytes(rd + kRdCtrlRecoil, cur, sizeof(cur)) && std::memcmp(cur, z4, sizeof(z4)) != 0) WriteT(rd + kRdCtrlRecoil, z4);
                WriteT(rd + kRdFireSway, z2);
            }
        }
        uintptr_t baf = 0;
        if (ReadRemote(local + kBafCamAnim, baf) && Plausible(baf)) {
            if (baf != g_baf) {
                if (g_baf) WriteT(g_baf + kBafScale, g_bafOrig);
                g_baf = baf;
                if (!ReadF(baf + kBafScale, g_bafOrig) || g_bafOrig > 100.0f) g_bafOrig = 1.0f;
            }
            float sc = 0.0f;
            if (ReadF(baf + kBafScale, sc) && sc != 0.0f) WriteT(baf + kBafScale, 0.0f);
        }
    }

    const int64_t& kAttrGadgetCd = OFF(AttrGadgetCooldown);
    const int64_t& kProxyCooldown = OFF(PerkProxyCooldown);
    uintptr_t g_cdSet = 0; float g_cdOrig = -1.0f;

    void UpdateCooldown(uintptr_t local) {
        const uintptr_t set = local ? CharSet(local, GetSurvivorStatus(local)) : 0;
        if (g_cdSet && (!g_Misc.NoDroneCooldown || set != g_cdSet)) {
            if (set == g_cdSet && g_cdOrig >= 0.0f && IsObjectAlive(g_cdSet)) WriteAttr(g_cdSet, kAttrGadgetCd, g_cdOrig);
            g_cdSet = 0; g_cdOrig = -1.0f;
        }
        if (!g_Misc.NoDroneCooldown) return;

        if (const HeldItem* h = World_HeldBy(local)) {
            static uintptr_t s_checked = 0; static bool s_isProxy = false;
            if (h->actor != s_checked) { s_checked = h->actor; s_isProxy = h->actor && IsClass(h->actor, "perkproxy"); }
            if (s_isProxy) {
                double rc = 0.0;
                if (ReadRemote(h->actor + kProxyCooldown, rc) && std::isfinite(rc)) {
                    if (rc > 0.0) WriteT(h->actor + kProxyCooldown, 0.0);
                }
            }
        }
        if (!set) return;
        float cur = 0.0f;
        if (!ReadF(set + kAttrGadgetCd + kCurrentValue, cur)) return;
        if (!g_cdSet) { g_cdSet = set; g_cdOrig = (cur >= 0.0f && cur < 10000.0f) ? cur : 0.0f; }
        else if (cur > 0.3f) g_cdOrig = cur;
        constexpr float kMinCd = 0.2f;
        if (std::fabs(cur - kMinCd) > 0.01f) WriteAttr(set, kAttrGadgetCd, kMinCd);
    }
}

namespace {
    const int64_t& kActorRole = OFF(ActorRole);
    int g_host = -1;
    int ReadHost(uintptr_t local) {
        uint8_t role = 0;
        if (!local || !ReadRemote(local + kActorRole, role) || role > 3) return -1;
        return role == 3 ? 1 : 0;
    }
}
int Misc_IsHost() { return g_host; }

namespace {
    uintptr_t g_world = 0;
    void ForgetAll(const char* why) {
        g_ftRun = false;
        if (g_ftThread.joinable()) g_ftThread.join();
        g_ftRoot = 0; g_ftMove = 0;
        const bool had = g_flyPawn || g_set || g_speedPawn || g_godStatus || g_godSet || g_rc.p || g_baf || g_cdSet || !g_frozen.empty();
        g_flyPawn = g_flyMove = 0; g_collOff = false;
        g_set = g_checkedSet = 0; g_setOk = false; g_slots.clear(); 
        g_speedPawn = 0; g_frozen.clear();
        g_godStatus = g_godSet = 0; g_godOrigHp = g_godOrigMax = -1.0f;
        g_rc = {}; g_baf = 0;
        g_cdSet = 0; g_cdOrig = -1.0f;
        g_tpLastPawn = 0;
        g_c2w = -1;
        if (had) Log_Add(Log_Info, "Misc: %s, dropped game pointers without writing", why);
    }
}

void Misc_Update(bool fg) {
    {
        const uintptr_t world = GetWorld();
        if (world != g_world) { if (g_world) ForgetAll("world changed"); g_world = world; }
        const uintptr_t lp = GetLocalPawn();
        if ((g_flyPawn && g_flyPawn != lp && !IsObjectAlive(g_flyPawn)) || (g_set && !IsObjectAlive(g_set)))
            ForgetAll("pawn gone");
        if (!world) return;
    }
    g_host = ReadHost(GetLocalPawn());
    if (g_host != 1) {
        MiscSettings saved = g_Misc;
        g_Misc.GodMode = g_Misc.Fly = g_Misc.Noclip = g_Misc.Teleport = false;
        g_Misc.Speedhack = g_Misc.FreezeBots = g_Misc.NoDroneCooldown = false;
        void Misc_UpdateInner(bool);
        Misc_UpdateInner(fg);
        g_Misc = saved;
        return;
    }
    void Misc_UpdateInner(bool);
    Misc_UpdateInner(fg);
}

void Misc_UpdateInner(bool fg) {
    UpdateAutoFire(fg);
    const bool any = g_Misc.NoRecoil || g_Misc.NoSpread || g_Misc.RapidFire || g_Misc.InfiniteAmmo
                  || g_Misc.Speedhack || g_Misc.FreezeBots || !g_slots.empty() || g_speedPawn || !g_frozen.empty()
                  || g_Misc.InstaReload || g_Misc.DamageBoost || g_Misc.WeaponSpeed || g_Misc.GodMode || g_godStatus || g_godSet || g_rc.p || !g_rdOrig.empty() || g_baf || g_Misc.NoDroneCooldown || g_cdSet
                  || g_Misc.Fly || g_Misc.Noclip || g_flyPawn || g_Misc.Teleport;
    if (!any) return;
    if (!EnsureHandle()) return;
    const uintptr_t local = GetLocalPawn();
    UpdateWeapon(local);
    UpdateTime(local);
    UpdateGod(local);
    UpdateRecoilAnim(local);
    UpdateCooldown(local);
    UpdateFly(local, fg);
    UpdateTeleport(local, fg);
}

void Misc_Restore() {
    StopAutoFire();
    if (!g_wh) return;
    const MiscSettings saved = g_Misc;
    g_Misc.NoRecoil = g_Misc.NoSpread = g_Misc.RapidFire = g_Misc.InfiniteAmmo = false;
    g_Misc.Speedhack = g_Misc.FreezeBots = false;
    g_Misc.InstaReload = g_Misc.DamageBoost = g_Misc.WeaponSpeed = false;
    g_Misc.NoDroneCooldown = false;
    g_Misc.GodMode = g_Misc.Fly = g_Misc.Noclip = g_Misc.Teleport = false;
    Misc_UpdateInner(false);
    g_Misc = saved;
    CloseHandle(g_wh);
    g_wh = nullptr;
    g_whPid = 0;
}

float Misc_GodOrigMax() { return g_godOrigMax; }
