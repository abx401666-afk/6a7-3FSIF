#include "../include/Misc.hpp"
#include "../include/Memory.hpp"
#include "../include/Log.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Targets.hpp"
#include "../include/UETypes.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
    constexpr int kPerChunk = 0x10000, kItemSize = 0x18, kSlice = 2048;

    HANDLE g_wh = nullptr; DWORD g_whPid = 0;
    std::vector<uintptr_t> g_fireClasses;
    int g_cursor = 0, g_total = 0;
    bool g_passDone = false;
    double g_passStart = -1.0;

    struct TArr { uintptr_t data; int32_t num, max; };
    bool Plausible(uintptr_t p) { return p > 0x10000 && p < 0x7FFFFFFFFFFFull && (p & 7) == 0; }

    std::string NameAt(uintptr_t obj) {
        uint32_t idx = 0;
        if (!ReadRemote(obj + OFF(ObjectName), idx)) return {};
        return Skeleton_FName(idx);
    }

    bool EnsureHandle() {
        const DWORD pid = GetAttachedProcessId();
        if (!pid) return false;
        if (g_wh && g_whPid == pid) return true;
        if (g_wh) CloseHandle(g_wh);
        g_wh = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
        g_whPid = pid;
        return g_wh != nullptr;
    }

    std::unordered_map<uintptr_t, bool> g_isBgc;
    bool IsFireClass(uintptr_t obj, uintptr_t cls) {
        auto it = g_isBgc.find(cls);
        if (it == g_isBgc.end()) {
            const std::string cn = NameAt(cls);
            if (cn.empty()) return false;
            it = g_isBgc.emplace(cls, cn == "blueprintgeneratedclass").first;
        }
        if (!it->second) return false;
        const std::string n = NameAt(obj);
        return n.rfind("ga_fire", 0) == 0;
    }

    void ScanSlice(bool wantFire) {
        const uintptr_t arr = GetRemoteModuleBase() + Offsets::GObjects;
        uintptr_t objects = 0; int32_t num = 0, chunks = 0;
        if (!ReadRemote(arr, objects) || !ReadRemote(arr + 0x14, num) || !ReadRemote(arr + 0x1C, chunks)) return;
        if (!Plausible(objects) || num <= 0 || num > 8 * 1024 * 1024 || chunks <= 0) return;
        g_total = num;
        if (g_cursor >= num) {
            g_passDone = true;
            if (Targets_Now() - g_passStart < 5.0) return;
            g_cursor = 0;
        }
        if (g_cursor == 0) {
            g_passStart = Targets_Now();
            g_passDone = false;
            g_fireClasses.clear();
        }
        const int chunk = g_cursor / kPerChunk, inChunk = g_cursor % kPerChunk;
        uintptr_t chunkPtr = 0;
        if (chunk >= chunks || !ReadRemote(objects + chunk * 8ull, chunkPtr) || !Plausible(chunkPtr)) { g_cursor = (chunk + 1) * kPerChunk; return; }
        const int count = (std::min)({ kSlice, kPerChunk - inChunk, num - g_cursor });
        std::vector<uint8_t> items(static_cast<size_t>(count) * kItemSize);
        if (!ReadRemoteBytes(chunkPtr + static_cast<uintptr_t>(inChunk) * kItemSize, items.data(), items.size())) { g_cursor += count; return; }
        for (int i = 0; i < count; ++i) {
            uintptr_t obj = 0;
            std::memcpy(&obj, items.data() + static_cast<size_t>(i) * kItemSize, 8);
            if (!Plausible(obj)) continue;
            struct { uint64_t vt; uint32_t flags; uint32_t index; uintptr_t cls; } head{};
            if (!ReadRemote(obj, head) || !Plausible(head.cls)) continue;
            if (head.flags & 0x10) continue;
            if (wantFire && IsFireClass(obj, head.cls)) g_fireClasses.push_back(obj);
        }
        g_cursor += count;
    }

    struct Patch { uintptr_t addr; std::vector<uint8_t> orig, now; };
    std::vector<Patch> g_wbPatches;
    double g_wbNextTry = 0.0;

    bool WriteBytes(uintptr_t a, const std::vector<uint8_t>& b) {
        SIZE_T n = 0;
        return g_wh && WriteProcessMemory(g_wh, reinterpret_cast<LPVOID>(a), b.data(), b.size(), &n) && n == b.size();
    }
    std::string PtrName(const std::vector<uint8_t>& c, size_t at) {
        if (at + 8 > c.size()) return {};
        uintptr_t v = 0; std::memcpy(&v, c.data() + at, 8);
        return Plausible(v) ? NameAt(v) : std::string();
    }
    uintptr_t FindFunc(uintptr_t cls, const char* name) {
        uintptr_t fn = 0;
        ReadRemote(cls + OFF(StructChildren), fn);
        for (int g = 0; Plausible(fn) && g < 1024; ++g) {
            if (NameAt(fn) == name) return fn;
            if (!ReadRemote(fn + OFF(FieldNext), fn)) break;
        }
        return 0;
    }
    void WbRelease() {
        for (const Patch& p : g_wbPatches) WriteBytes(p.addr, p.orig);
        g_wbPatches.clear();
    }
    bool WbBuild(bool enable = true) {
        uintptr_t base = 0;
        for (uintptr_t c : g_fireClasses) if (NameAt(c) == "ga_fire_base_c") base = c;
        if (!base) { return false; }
        const uintptr_t fn = FindFunc(base, "firelogic");
        TArr sc{};
        if (!fn || !ReadRemote(fn + OFF(StructScript), sc) || sc.num < 64 || sc.num > 1 << 20 || !Plausible(sc.data & ~7ull)) { return false; }
        std::vector<uint8_t> c(static_cast<size_t>(sc.num));
        if (!ReadRemoteBytes(sc.data, c.data(), c.size())) { return false; }
        std::vector<Patch> ps;
        const double one = 1.0, q = 0.25, neg = -1.0;
        size_t rowAt = 0, mulAt = 0;
        bool mulPatched = false;
        for (size_t i = 0; i + 28 <= c.size(); ++i) {
            if (c[i] != 0x1C && c[i] != 0x68) continue;
            const std::string n = PtrName(c, i + 1);
            if (n == "getdatatablerowfromname") rowAt = i;
            else if (rowAt && n == "multiply_doubledouble" && c[i + 9] == 0x00 && c[i + 27] == 0x16) {
                if (c[i + 18] == 0x00) { mulAt = i; mulPatched = false; }
                else if (c[i + 18] == 0x37 && std::memcmp(c.data() + i + 19, &one, 8) == 0) { mulAt = i; mulPatched = true; }
            }
        }
        if (!mulAt) { return false; }
        Patch p1{ sc.data + mulAt + 18, std::vector<uint8_t>(c.begin() + mulAt + 18, c.begin() + mulAt + 27), std::vector<uint8_t>(9) };
        p1.now[0] = 0x37; std::memcpy(p1.now.data() + 1, &one, 8);
        if (mulPatched) {
            bool rebuilt = false;
            for (size_t j = mulAt; j-- > rowAt && j + 20 <= mulAt;) {
                if (c[j] == 0x0F && c[j + 9] == 0x00 && c[j + 18] == 0x38) {
                    p1.orig.assign(9, 0);
                    p1.orig[0] = 0x00; std::memcpy(p1.orig.data() + 1, c.data() + j + 10, 8);
                    rebuilt = true; break;
                }
            }
            if (!rebuilt) { return false; }
        }
        ps.push_back(p1);
        int found = 0;
        for (size_t v = 19; v + 8 <= c.size(); ++v) {
            if (c[v - 1] != 0x37 || c[v - 10] != 0x00 || c[v - 19] != 0x68) continue;
            const bool isQ = std::memcmp(c.data() + v, &q, 8) == 0, isNeg = std::memcmp(c.data() + v, &neg, 8) == 0;
            if (!isQ && !isNeg) continue;
            if (PtrName(c, v - 18) != "greater_doubledouble") continue;
            Patch p2{ sc.data + v, std::vector<uint8_t>(8), std::vector<uint8_t>(8) };
            std::memcpy(p2.orig.data(), &q, 8);
            std::memcpy(p2.now.data(), &neg, 8);
            ps.push_back(p2); ++found;
        }
        if (found != 1) { return false; }
        if (!enable) {
            int fixedCount = 0;
            for (const Patch& p : ps) {
                const size_t off = static_cast<size_t>(p.addr - sc.data);
                if (off + p.orig.size() > c.size() || std::memcmp(c.data() + off, p.orig.data(), p.orig.size()) == 0) continue;
                if (WriteBytes(p.addr, p.orig)) ++fixedCount;
            }
            if (fixedCount) Log_Add(Log_Info, "Wallbang: was still active with the toggle off, FireLogic restored");
            return true;
        }
        if (mulPatched) Log_Add(Log_Info, "Wallbang: FireLogic was still patched from an earlier run, taking it over");
        for (const Patch& p : ps) if (!WriteBytes(p.addr, p.now)) { for (const Patch& r : ps) WriteBytes(r.addr, r.orig); return false; }
        g_wbPatches = std::move(ps);
        Log_Add(Log_Info, "Wallbang: FireLogic patched (power multiply @+0x%zX, threshold)", mulAt);
        return true;
    }
    void UpdateWallbang() {
        if (!g_wbPatches.empty()) {
            std::vector<uint8_t> cur(g_wbPatches[0].now.size());
            if (ReadRemoteBytes(g_wbPatches[0].addr, cur.data(), cur.size()) && cur == g_wbPatches[0].now) { return; }
            g_wbPatches.clear();
        }
        if (Targets_Now() < g_wbNextTry) return;
        if (!WbBuild()) g_wbNextTry = g_passDone ? Targets_Now() + 3.0 : 0.0;
    }
}

void Host_Update() {
    if (!GetAttachedProcessId()) return;
    const bool wall = g_Misc.Wallbang && Misc_IsHost() == 1;
    if (!wall && !g_wbPatches.empty() && EnsureHandle()) WbRelease();
    static double offCheckAt = 0.0;
    uintptr_t base = 0;
    for (uintptr_t c : g_fireClasses) if (NameAt(c) == "ga_fire_base_c") { base = c; break; }
    const bool offCheck = !wall && Targets_Now() >= offCheckAt;
    const bool needFire = (wall && g_wbPatches.empty()) || (offCheck && !base);
    static bool lastNeedFire = false;
    if (needFire && !lastNeedFire && g_fireClasses.empty()) { g_cursor = 0; g_passStart = -1.0; g_passDone = false; }
    lastNeedFire = needFire;
    if (!needFire && !wall && !offCheck) return;
    if (needFire) ScanSlice(true);
    if (offCheck && (base || g_passDone)) {
        offCheckAt = Targets_Now() + (base ? 3.0 : 10.0);
        if (base && EnsureHandle()) WbBuild(false);
    }
    if (wall && EnsureHandle()) UpdateWallbang();
}

namespace {
    const int64_t& kCtrlBits = OFF(ControllerBits);
    const int64_t& kIgnoreMove = OFF(ControllerIgnoreMove);
    const int64_t& kIgnoreLook = OFF(ControllerIgnoreLook);
    const int64_t& kCtrlRot = BodycamOffsets::ControllerControlRotation;
    struct Rot3 { double p, y, r; };
    uintptr_t g_frozenCtrl = 0;
    uint8_t   g_origMove = 0, g_origLook = 0;
    std::atomic<bool>      g_pinRun{ false };
    std::atomic<uintptr_t> g_pinCtrl{ 0 };
    std::thread            g_pinThread;
    Rot3                   g_pinRot{};

    void PinThread() {
        while (g_pinRun) {
            const uintptr_t pc = g_pinCtrl;
            if (pc && g_wh && !IsObjectAlive(pc)) g_pinCtrl = 0;
            else if (pc && g_wh) {
                Rot3 cur{};
                if (ReadRemote(pc + kCtrlRot, cur) && std::memcmp(&cur, &g_pinRot, sizeof(cur)) != 0) {
                    SIZE_T n = 0;
                    WriteProcessMemory(g_wh, reinterpret_cast<LPVOID>(pc + kCtrlRot), &g_pinRot, sizeof(g_pinRot), &n);
                }
            }
            Sleep(1);
        }
    }

    void UnfreezeInput() {
        g_pinRun = false;
        if (g_pinThread.joinable()) g_pinThread.join();
        g_pinCtrl = 0;
        if (g_frozenCtrl && g_wh && IsObjectAlive(g_frozenCtrl)) {
            SIZE_T n = 0;
            WriteProcessMemory(g_wh, reinterpret_cast<LPVOID>(g_frozenCtrl + kIgnoreMove), &g_origMove, 1, &n);
            WriteProcessMemory(g_wh, reinterpret_cast<LPVOID>(g_frozenCtrl + kIgnoreLook), &g_origLook, 1, &n);
        }
        g_frozenCtrl = 0;
    }
}

void Host_FreezeInput(bool on) {
    const uintptr_t pc = on ? GetLocalPlayerController() : 0;
    if (g_frozenCtrl && g_frozenCtrl != pc) UnfreezeInput();
    if (!on || !pc || !EnsureHandle()) return;
    uint8_t bits = 0, mv = 0, lk = 0;
    if (!ReadRemote(pc + kCtrlBits, bits) || !(bits & 0x02)) return;
    if (!ReadRemote(pc + kIgnoreMove, mv) || !ReadRemote(pc + kIgnoreLook, lk) || mv > 16 || lk > 16) return;
    if (g_frozenCtrl != pc) {
        Rot3 r{};
        if (!ReadRemote(pc + kCtrlRot, r) || !std::isfinite(r.p) || !std::isfinite(r.y) || !std::isfinite(r.r)) return;
        g_frozenCtrl = pc; g_origMove = mv; g_origLook = lk;
        g_pinRot = r;
        g_pinCtrl = pc;
        g_pinRun = true;
        g_pinThread = std::thread(PinThread);
    }
    const uint8_t one = 1;
    SIZE_T n = 0;
    if (lk == 0) WriteProcessMemory(g_wh, reinterpret_cast<LPVOID>(pc + kIgnoreLook), &one, 1, &n);
    if (mv == 0) WriteProcessMemory(g_wh, reinterpret_cast<LPVOID>(pc + kIgnoreMove), &one, 1, &n);
}

void Host_Restore() {
    UnfreezeInput();
    if (g_wh) {
        if (!g_wbPatches.empty()) WbRelease();
        CloseHandle(g_wh);
        g_wh = nullptr; g_whPid = 0;
    }
}
