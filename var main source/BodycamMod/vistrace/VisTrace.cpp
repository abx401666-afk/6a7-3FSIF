#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include "MinHook.h"
#include "../include/VisShared.hpp"

namespace {
    uintptr_t kGObjects = 0, kGWorld = 0, kAppendString = 0, kProcessEvent = 0;
    int       kProcessEventIdx = 0;
    uintptr_t kObjClass = 0, kObjName = 0, kObjOuter = 0;
    uintptr_t kFieldNext = 0, kStructSuper = 0, kStructChildren = 0;
    uintptr_t kClassDefaultObject = 0, kObjArrayNum = 0, kWorldLevel = 0;
    uintptr_t kWorldGameInstance = 0, kGiLocalPlayers = 0;
    uintptr_t kLpController = 0, kPcAcknowledgedPawn = 0;
    uintptr_t kActorOwner = 0;
    size_t    hLocation = 0, hComponent = 0;
    constexpr int kChunk = 0x10000, kItemSize = 0x18;

    constexpr size_t kParmSize = 0x188;
    constexpr size_t pWorld = 0x00, pStart = 0x08, pEnd = 0x20, pChannel = 0x38, pComplex = 0x39,
                     pIgnore = 0x40, pDebug = 0x50, pHit = 0x58, pIgnoreSelf = 0x158, pRet = 0x180;

    using ProcessEventFn = void (*)(void*, void*, void*);
    using AppendStringFn = void (*)(const void*, void*);
    struct FString { wchar_t* data; int32_t num; int32_t max; };
    struct TArrayPtr { void** data; int32_t num; int32_t max; };

    uintptr_t       g_base = 0;
    ProcessEventFn  g_orig = nullptr;
    DWORD           g_gameThread = 0;
    HANDLE          g_map = nullptr;
    VisShared::Block* g_sh = nullptr;
    void*           g_traceFn = nullptr;
    void*           g_kslCdo = nullptr;
    bool            g_initTried = false;
    uint32_t        g_lastReq = 0;
    thread_local bool t_busy = false;

    void Status(const char* s) { if (g_sh) { strncpy(g_sh->status, s, sizeof(g_sh->status) - 1); g_sh->status[sizeof(g_sh->status) - 1] = 0; } }

    template <class T> T At(uintptr_t a) { return *reinterpret_cast<T*>(a); }

    uintptr_t ObjectAt(int32_t index, int32_t serial) {
        if (index < 0) return 0;
        const uintptr_t arr = g_base + kGObjects;
        const uintptr_t chunks = At<uintptr_t>(arr);
        const int32_t num = At<int32_t>(arr + kObjArrayNum);
        if (!chunks || index >= num) return 0;
        const uintptr_t chunk = At<uintptr_t>(chunks + static_cast<uintptr_t>(index / kChunk) * 8);
        if (!chunk) return 0;
        const uintptr_t item = chunk + static_cast<uintptr_t>(index % kChunk) * kItemSize;
        if (serial != 0 && At<int32_t>(item + 0x10) != serial) return 0;
        return At<uintptr_t>(item);
    }

    bool NameIs(uintptr_t obj, const wchar_t* want) {
        static wchar_t buf[512];
        FString s{ buf, 0, 512 };
        reinterpret_cast<AppendStringFn>(g_base + kAppendString)(reinterpret_cast<const void*>(obj + kObjName), &s);
        if (s.data != buf || s.num <= 0) return false;
        const int n = s.num - 1;
        return n >= 0 && static_cast<size_t>(n) == wcslen(want) && wcsncmp(buf, want, n) == 0;
    }

    void FindTraceFunction() {
        g_initTried = true;
        const uintptr_t arr = g_base + kGObjects;
        const int32_t num = At<int32_t>(arr + kObjArrayNum);
        for (int32_t i = 0; i < num; ++i) {
            const uintptr_t obj = ObjectAt(i, 0);
            if (!obj) continue;
            const uintptr_t cls = At<uintptr_t>(obj + kObjClass);
            if (!cls || At<uintptr_t>(cls + kObjClass) != cls) continue;
            if (!NameIs(obj, L"KismetSystemLibrary")) continue;
            const uintptr_t cdo = At<uintptr_t>(obj + kClassDefaultObject);
            for (uintptr_t f = At<uintptr_t>(obj + kStructChildren); f; f = At<uintptr_t>(f + kFieldNext)) {
                if (NameIs(f, L"LineTraceSingle")) { g_traceFn = reinterpret_cast<void*>(f); g_kslCdo = reinterpret_cast<void*>(cdo); break; }
            }
            break;
        }
        if (g_traceFn && g_kslCdo) { g_sh->dllState = 2; Status("ready"); }
        else { g_sh->dllState = 3; Status("LineTraceSingle not found"); }
    }

    uintptr_t LocalPawn(uintptr_t world) {
        const uintptr_t gi = At<uintptr_t>(world + kWorldGameInstance);
        if (!gi) return 0;
        const TArrayPtr lps = At<TArrayPtr>(gi + kGiLocalPlayers);
        if (!lps.data || lps.num < 1) return 0;
        const uintptr_t lp = reinterpret_cast<uintptr_t>(lps.data[0]);
        if (!lp) return 0;
        const uintptr_t pc = At<uintptr_t>(lp + kLpController);
        return pc ? At<uintptr_t>(pc + kPcAcknowledgedPawn) : 0;
    }

    int WhoOwns(uintptr_t component, uintptr_t target, uintptr_t local) {
        const uintptr_t actor = At<uintptr_t>(component + kObjOuter);
        if (!actor) return 0;
        if (actor == target) return 1;
        if (local && actor == local) return 2;
        uintptr_t owner = At<uintptr_t>(actor + kActorOwner);
        for (int k = 0; owner && k < 3; ++k) {
            if (owner == target) return 1;
            if (local && owner == local) return 2;
            owner = At<uintptr_t>(owner + kActorOwner);
        }
        return 0;
    }

    bool Trace(uintptr_t world, uintptr_t local, const VisShared::Ray& r) {
        alignas(16) uint8_t p[kParmSize];
        void* ignore[1] = { reinterpret_cast<void*>(local) };
        double sx = r.sx, sy = r.sy, sz = r.sz;
        const double dx = r.ex - r.sx, dy = r.ey - r.sy, dz = r.ez - r.sz;
        const double len = sqrt(dx * dx + dy * dy + dz * dz);
        if (!(len > 1.0)) return true;
        const double ux = dx / len, uy = dy / len, uz = dz / len;
        for (int hop = 0; hop < 4; ++hop) {
            std::memset(p, 0, sizeof(p));
            *reinterpret_cast<uintptr_t*>(p + pWorld) = world;
            double* s = reinterpret_cast<double*>(p + pStart);
            double* e = reinterpret_cast<double*>(p + pEnd);
            s[0] = sx; s[1] = sy; s[2] = sz;
            e[0] = r.ex; e[1] = r.ey; e[2] = r.ez;
            p[pChannel] = 0;
            p[pComplex] = 1;
            TArrayPtr ig{ ignore, local ? 1 : 0, 1 };
            std::memcpy(p + pIgnore, &ig, sizeof(ig));
            p[pDebug] = 0;
            p[pIgnoreSelf] = 0;
            g_orig(g_kslCdo, g_traceFn, p);
            if (!p[pRet]) return true;
            const int32_t* weak = reinterpret_cast<const int32_t*>(p + pHit + hComponent);
            const uintptr_t comp = ObjectAt(weak[0], weak[1]);
            const int who = comp ? WhoOwns(comp, r.target, local) : 0;
            if (who == 1) return true;
            if (who != 2) return false;
            const double* hl = reinterpret_cast<const double*>(p + pHit + hLocation);
            sx = hl[0] + ux * 2.0; sy = hl[1] + uy * 2.0; sz = hl[2] + uz * 2.0;
            if ((sx - r.sx) * ux + (sy - r.sy) * uy + (sz - r.sz) * uz >= len) return true;
        }
        return false;
    }

    void RunRequests() {
        if (!g_sh || !g_sh->enabled) return;
        const uint32_t seq = g_sh->reqSeq;
        if (seq == g_lastReq || (seq & 1)) return;
        if (!g_initTried) FindTraceFunction();
        if (!g_traceFn) { g_lastReq = seq; return; }
        const uintptr_t world = At<uintptr_t>(g_base + kGWorld);
        if (!world || !At<uintptr_t>(world + kWorldLevel)) return;
        const uintptr_t local = LocalPawn(world);
        if (!local) return;

        static VisShared::Ray rays[VisShared::kMaxRays];
        uint32_t count = g_sh->count;
        if (count > static_cast<uint32_t>(VisShared::kMaxRays)) count = VisShared::kMaxRays;
        std::memcpy(rays, const_cast<const VisShared::Ray*>(g_sh->rays), count * sizeof(VisShared::Ray));
        MemoryBarrier();
        if (g_sh->reqSeq != seq) return;
        g_lastReq = seq;

        static uint8_t vis[VisShared::kMaxRays];
        for (uint32_t i = 0; i < count; ++i) vis[i] = Trace(world, local, rays[i]) ? 1 : 0;

        g_sh->resSeq = g_sh->resSeq + 1;
        MemoryBarrier();
        for (uint32_t i = 0; i < count; ++i) { g_sh->resTarget[i] = rays[i].target; g_sh->resVisible[i] = vis[i]; }
        g_sh->resCount = count;
        MemoryBarrier();
        g_sh->resSeq = g_sh->resSeq + 1;
        g_sh->tracesDone += count;
        g_sh->heartbeat = GetTickCount64();
    }

    void Detour(void* self, void* fn, void* parms) {
        if (!t_busy && GetCurrentThreadId() == g_gameThread) {
            t_busy = true;
            RunRequests();
            t_busy = false;
        }
        g_orig(self, fn, parms);
    }

    DWORD MainThreadId() {
        DWORD best = 0; ULONGLONG bestTime = ~0ull;
        const DWORD pid = GetCurrentProcessId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        THREADENTRY32 te{ sizeof(te) };
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
            if (!h) continue;
            FILETIME c, x, k, u;
            if (GetThreadTimes(h, &c, &x, &k, &u)) {
                const ULONGLONG t = (static_cast<ULONGLONG>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
                if (t < bestTime) { bestTime = t; best = te.th32ThreadID; }
            }
            CloseHandle(h);
        }
        CloseHandle(snap);
        return best;
    }

    DWORD WINAPI Init(LPVOID) {
        g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(VisShared::Block), BCVIS_MAP_NAME);
        if (!g_map) return 0;
        g_sh = static_cast<VisShared::Block*>(MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VisShared::Block)));
        if (!g_sh) return 0;
        g_sh->magic = VisShared::kMagic;
        g_sh->version = VisShared::kVersion;
        g_sh->dllState = 0;
        g_sh->pid = GetCurrentProcessId();
        g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!g_sh->offsetsReady) { g_sh->dllState = 3; Status("no offsets from var.exe"); return 0; }
        const VisShared::Offsets& o = g_sh->off;
        kGObjects = o.gobjects; kGWorld = o.gworld; kAppendString = o.appendString; kProcessEvent = o.processEvent;
        kProcessEventIdx = static_cast<int>(o.processEventIdx);
        kObjClass = o.objClass; kObjName = o.objName; kObjOuter = o.objOuter;
        kFieldNext = o.fieldNext; kStructSuper = o.structSuper; kStructChildren = o.structChildren;
        kClassDefaultObject = o.classDefaultObject; kObjArrayNum = o.objArrayNum; kWorldLevel = o.worldPersistentLevel;
        kWorldGameInstance = o.worldGameInstance; kGiLocalPlayers = o.giLocalPlayers;
        kLpController = o.lpController; kPcAcknowledgedPawn = o.pcAckPawn; kActorOwner = o.actorOwner;
        hLocation = static_cast<size_t>(o.hitLocation); hComponent = static_cast<size_t>(o.hitComponent);
        if (!kGObjects || !kGWorld || !kAppendString || !kProcessEvent || kProcessEventIdx <= 0) {
            g_sh->dllState = 3; Status("offsets from var.exe incomplete"); return 0;
        }
        g_gameThread = MainThreadId();
        if (!g_gameThread) { g_sh->dllState = 3; Status("game thread not found"); return 0; }

        void* target = reinterpret_cast<void*>(g_base + kProcessEvent);
        for (int i = 0; i < 64; ++i) {
            const uintptr_t obj = ObjectAt(i, 0);
            if (!obj) continue;
            const uintptr_t vt = At<uintptr_t>(obj);
            if (vt) { target = At<void*>(vt + kProcessEventIdx * 8); break; }
        }
        if (MH_Initialize() != MH_OK || MH_CreateHook(target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_orig)) != MH_OK ||
            MH_EnableHook(target) != MH_OK) {
            g_sh->dllState = 3; Status("hook failed");
            return 0;
        }
        g_sh->dllState = 1;
        Status("hooked, waiting for the game thread");
        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        if (HANDLE t = CreateThread(nullptr, 0, Init, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
