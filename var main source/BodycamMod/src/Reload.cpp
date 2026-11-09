#include "../include/Reload.hpp"
#include "../include/Memory.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Targets.hpp"
#include "../include/World.hpp"
#include "../include/Log.hpp"
#include <cctype>
#include <cstdio>
#include <unordered_set>
#include <string>
#include <vector>

namespace {
    const int64_t& kAnimInstance = OFF(SkeletalAnimInstance);
    const int64_t& kWepBody = OFF(ItemWepBody);
    const int64_t& kPlaying = OFF(MontagePlaying);
    ptrdiff_t g_arrOff = -1;
    bool g_active = false;
    std::string g_seen;
    double g_lastProbe = -1.0;

    bool Plausible(uintptr_t p) { return p > 0x10000 && p < 0x7FFFFFFFFFFFull && (p & 7) == 0; }
    struct TArr { uintptr_t data; int32_t num, max; };

    bool IsMontage(uintptr_t obj) {
        if (!Plausible(obj)) return false;
        for (const std::string& n : Skeleton_ClassChain(obj)) if (n == "animmontage") return true;
        return false;
    }

    void Probe(uintptr_t inst) {
        for (ptrdiff_t off = 0x100; off < 0x900; off += 8) {
            TArr a{};
            if (!ReadRemote(inst + off, a) || !Plausible(a.data) || a.num < 1 || a.num > 16 || a.max < a.num || a.max > 64) continue;
            uintptr_t mi = 0, m = 0;
            if (!ReadRemote(a.data, mi) || !Plausible(mi) || !ReadRemote(mi, m)) continue;
            if (IsMontage(m)) { g_arrOff = off; return; }
        }
    }

    bool Scan(uintptr_t inst, std::string& names) {
        if (!Plausible(inst)) return false;
        if (g_arrOff < 0) {
            const double now = Targets_Now();
            if (now - g_lastProbe < 0.1) return false;
            g_lastProbe = now;
            Probe(inst);
            if (g_arrOff < 0) return false;
        }
        TArr a{};
        if (!ReadRemote(inst + g_arrOff, a) || !Plausible(a.data) || a.num < 0 || a.num > 16) return false;
        bool reload = false;
        for (int i = 0; i < a.num; ++i) {
            uintptr_t mi = 0, m = 0;
            bool playing = false;
            if (!ReadRemote(a.data + i * 8ull, mi) || !Plausible(mi) || !ReadRemote(mi, m) || !Plausible(m)) continue;
            ReadRemote(mi + kPlaying, playing);
            uint32_t idx = 0;
            if (!ReadRemote(m + OFF(ObjectName), idx)) continue;
            const std::string n = Skeleton_FName(idx);
            if (n.empty()) continue;
            if (names.size() < 90) names += (names.empty() ? "" : ", ") + n;
            {
                static std::unordered_set<std::string> logged;
                if (logged.size() < 200 && logged.insert(n).second) Log_Add(Log_Info, "Reload: montage seen \"%s\"", n.c_str());
            }
            std::string l = n;
            for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            static const char* const kKeys[] = { "reload", "_rld", "load", "insert", "cylinder", "eject", "refill", "speedl" };
            if (playing) for (const char* k : kKeys) if (l.find(k) != std::string::npos) { reload = true; break; }
        }
        return reload;
    }
}

void Reload_Update() {
    g_active = false;
    const uintptr_t pawn = Targets_Get().localPawn;
    if (!pawn) return;
    std::string names;
    const uintptr_t mesh = GetMeshComponent(pawn);
    if (mesh) g_active |= Scan(ReadPtr(mesh, kAnimInstance), names);
    if (const HeldItem* h = World_HeldBy(pawn)) {
        const uintptr_t wm = ReadPtr(h->actor, kWepBody);
        if (wm) g_active |= Scan(ReadPtr(wm, kAnimInstance), names);
    }
    if (!names.empty()) g_seen = names;
}

bool Reload_Active() { return g_active; }

std::string Reload_Status() {
    char b[160];
    if (g_arrOff < 0) std::snprintf(b, sizeof(b), "Reload anims: not seen yet (fire or reload once)");
    else std::snprintf(b, sizeof(b), "Anims +0x%X: %s", static_cast<unsigned>(g_arrOff), g_seen.empty() ? "-" : g_seen.c_str());
    return b;
}
