#include "../include/Visibility.hpp"
#include "../include/Targets.hpp"
#include "../include/Memory.hpp"
#include "../include/Log.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "../include/VisShared.hpp"

namespace {
    float g_time = 0.0f;
    bool  g_haveTime = false;

    float FrameWindow() {
        return static_cast<float>((std::clamp)(Targets_GameFrameTime() * 2.0 + 0.01, 0.03, 0.12));
    }
    bool Fresh(float v) {
        return std::isfinite(v) && v > 1.0f && v > g_time - FrameWindow() && v <= g_time + 0.05f;
    }
}

namespace los {
    HANDLE            g_map = nullptr;
    VisShared::Block* g_sh = nullptr;
    DWORD             g_injectedPid = 0;
    ULONGLONG         g_nextTry = 0;
    std::unordered_map<uintptr_t, bool> g_verdict;

    constexpr uint64_t kProbeTag  = 0xB0B0000000000000ull, kProbeMask = 0xFFFF000000000000ull;
    constexpr int      kProbeRays = 32, kProbeStages = 2, kMaxProbes = 3;
    struct Probe {
        uint32_t id = 0;
        FVector  s{}, u{};
        double   lo = 0.0, hi = 0.0, d[kProbeRays] = {};
        int      stage = 0;
        int      state = 0;
        double   dist = -1.0;
        ULONGLONG started = 0, doneAt = 0;
    };
    std::vector<Probe> g_probes;
    uint32_t g_probeNext = 1;

    void ProbeDists(Probe& pr) {
        for (int i = 0; i < kProbeRays; ++i) {
            const double f = static_cast<double>(i + 1) / kProbeRays;
            pr.d[i] = pr.lo + (pr.hi - pr.lo) * (pr.stage == 0 ? f * f : f);
        }
    }
    ULONGLONG         g_verdictAt = 0;
    uint32_t          g_lastRes = 0;
    bool              g_loggedReady = false, g_loggedFail = false;

    bool Map() {
        if (g_sh) return true;
        g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(VisShared::Block), BCVIS_MAP_NAME);
        if (!g_map) return false;
        g_sh = static_cast<VisShared::Block*>(MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VisShared::Block)));
        if (!g_sh) return false;
        VisShared::Offsets& o = g_sh->off;
        o.gobjects = OFF(GObjects); o.gworld = OFF(GWorld); o.appendString = OFF(AppendString);
        o.processEvent = OFF(ProcessEvent); o.processEventIdx = OFF(ProcessEventIdx);
        o.objClass = OFF(ObjectClass); o.objName = OFF(ObjectName); o.objOuter = OFF(ObjectOuter);
        o.fieldNext = OFF(FieldNext); o.structSuper = OFF(StructSuper); o.structChildren = OFF(StructChildren);
        o.classDefaultObject = OFF(ClassDefaultObject); o.objArrayNum = OFF(ObjArrayNumElements);
        o.worldPersistentLevel = OFF(WorldPersistentLevel); o.worldGameInstance = OFF(WorldOwningGameInstance);
        o.giLocalPlayers = OFF(GameInstanceLocalPlayers); o.lpController = OFF(LocalPlayerController);
        o.pcAckPawn = OFF(ControllerAcknowledgedPawn); o.actorOwner = OFF(ActorOwner);
        o.hitLocation = OFF(HitResultLocation); o.hitComponent = OFF(HitResultComponent);
        MemoryBarrier();
        g_sh->offsetsReady = 1;
        return true;
    }

    std::wstring ExtractDll() {
        HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(201), MAKEINTRESOURCEW(10)  );
        if (!r) return {};
        HGLOBAL g = LoadResource(nullptr, r);
        const DWORD size = SizeofResource(nullptr, r);
        const uint8_t* data = g ? static_cast<const uint8_t*>(LockResource(g)) : nullptr;
        if (!data || size < 1024) return {};
        uint32_t h = 2166136261u;
        for (DWORD i = 0; i < size; ++i) { h ^= data[i]; h *= 16777619u; }
        const wchar_t* appdata = _wgetenv(L"APPDATA");
        if (!appdata || !*appdata) return {};
        const std::wstring dir = std::wstring(appdata) + L"\\BodycamMod";
        CreateDirectoryW(dir.c_str(), nullptr);
        wchar_t name[64];
        swprintf(name, 64, L"\\bcvis_%08x.dll", h);
        const std::wstring path = dir + name;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) return {};
            DWORD w = 0;
            const BOOL ok = WriteFile(f, data, size, &w, nullptr);
            CloseHandle(f);
            if (!ok || w != size) { DeleteFileW(path.c_str()); return {}; }
        }
        return path;
    }

    bool Inject(DWORD pid) {
        const std::wstring path = ExtractDll();
        if (path.empty()) { Log_Add(Log_Error, "Line of sight: could not unpack the helper DLL"); return false; }
        HANDLE p = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (!p) { Log_Add(Log_Error, "Line of sight: cannot open the game process (%lu)", GetLastError()); return false; }
        const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
        void* mem = VirtualAllocEx(p, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        bool ok = false;
        if (mem && WriteProcessMemory(p, mem, path.c_str(), bytes, nullptr)) {
            auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
            if (HANDLE t = CreateRemoteThread(p, nullptr, 0, loadLib, mem, 0, nullptr)) {
                ok = WaitForSingleObject(t, 5000) == WAIT_OBJECT_0;
                DWORD code = 0;
                if (ok && GetExitCodeThread(t, &code) && code == 0) ok = false;
                CloseHandle(t);
            }
        }
        if (mem) VirtualFreeEx(p, mem, 0, MEM_RELEASE);
        CloseHandle(p);
        Log_Add(ok ? Log_Info : Log_Error, ok ? "Line of sight: helper loaded into the game" : "Line of sight: helper failed to load");
        return ok;
    }

    bool Active() {
        return g_sh && g_sh->magic == VisShared::kMagic && g_sh->version == VisShared::kVersion && g_sh->pid == GetAttachedProcessId() &&
               g_sh->dllState == 2 && GetTickCount64() - g_sh->heartbeat < 600;
    }

    void Post() {
        const TargetFrame& f = Targets_Get();
        static VisShared::Ray rays[VisShared::kMaxRays];
        uint32_t n = 0;
        if (f.camValid) {
            const FVector cam = f.cam.Location;
            for (const Target& t : f.targets) {
                if (t.drone || !t.pawn || n + 5 > static_cast<uint32_t>(VisShared::kMaxRays)) continue;
                const double hh = t.halfHeight > 10.0f ? t.halfHeight : 92.0;
                const double rad = t.radius > 5.0f ? t.radius : 42.0;
                double rx = -(t.pos.Y - cam.Y), ry = t.pos.X - cam.X;
                const double rl = std::sqrt(rx * rx + ry * ry);
                if (rl > 1.0) { rx = rx / rl * rad * 0.75; ry = ry / rl * rad * 0.75; } else { rx = ry = 0.0; }
                const double pts[5][3] = {
                    { t.pos.X,      t.pos.Y,      t.pos.Z + hh * 0.80 },
                    { t.pos.X,      t.pos.Y,      t.pos.Z + hh * 0.35 },
                    { t.pos.X + rx, t.pos.Y + ry, t.pos.Z + hh * 0.35 },
                    { t.pos.X - rx, t.pos.Y - ry, t.pos.Z + hh * 0.35 },
                    { t.pos.X,      t.pos.Y,      t.pos.Z },
                };
                for (const auto& q : pts) rays[n++] = { cam.X, cam.Y, cam.Z, q[0], q[1], q[2], static_cast<uint64_t>(t.pawn) };
            }
        }
        for (Probe& pr : g_probes) {
            if (pr.state != 0 || n + kProbeRays > static_cast<uint32_t>(VisShared::kMaxRays)) continue;
            for (int i = 0; i < kProbeRays; ++i) {
                const FVector e = pr.s + pr.u * pr.d[i];
                const uint64_t tag = kProbeTag | (static_cast<uint64_t>(pr.id & 0xFFFFFF) << 16) | (static_cast<uint64_t>(pr.stage) << 8) | static_cast<uint64_t>(i);
                rays[n++] = { pr.s.X, pr.s.Y, pr.s.Z, e.X, e.Y, e.Z, tag };
            }
        }
        const uint32_t seq = g_sh->reqSeq;
        g_sh->reqSeq = (seq | 1u);
        MemoryBarrier();
        std::memcpy(const_cast<VisShared::Ray*>(g_sh->rays), rays, n * sizeof(VisShared::Ray));
        g_sh->count = n;
        g_sh->enabled = 1;
        MemoryBarrier();
        g_sh->reqSeq = (seq | 1u) + 1;
    }

    void Read() {
        const uint32_t a = g_sh->resSeq;
        if ((a & 1) || a == g_lastRes) return;
        MemoryBarrier();
        uint32_t n = g_sh->resCount;
        if (n > static_cast<uint32_t>(VisShared::kMaxRays)) n = VisShared::kMaxRays;
        std::unordered_map<uintptr_t, bool> v;
        struct PR { uint32_t id; int stage, idx; bool clear; };
        std::vector<PR> probeRes;
        for (uint32_t i = 0; i < n; ++i) {
            const uint64_t tg = g_sh->resTarget[i];
            if ((tg & kProbeMask) == kProbeTag) {
                probeRes.push_back({ static_cast<uint32_t>((tg >> 16) & 0xFFFFFF), static_cast<int>((tg >> 8) & 0xFF),
                                     static_cast<int>(tg & 0xFF), g_sh->resVisible[i] != 0 });
                continue;
            }
            bool& slot = v[static_cast<uintptr_t>(tg)];
            slot = slot || g_sh->resVisible[i] != 0;
        }
        MemoryBarrier();
        if (g_sh->resSeq != a) return;
        g_lastRes = a;
        g_verdict.swap(v);
        g_verdictAt = GetTickCount64();

        for (Probe& pr : g_probes) {
            if (pr.state != 0) continue;
            int firstBlocked = kProbeRays, got = 0;
            for (const PR& r : probeRes) {
                if (r.id != (pr.id & 0xFFFFFF) || r.stage != pr.stage || r.idx < 0 || r.idx >= kProbeRays) continue;
                ++got;
                if (!r.clear && r.idx < firstBlocked) firstBlocked = r.idx;
            }
            if (got < kProbeRays) continue;
            if (firstBlocked == kProbeRays) {
                pr.state = 1; pr.dist = pr.hi; pr.doneAt = GetTickCount64();
                continue;
            }
            const double lo = firstBlocked == 0 ? pr.lo : pr.d[firstBlocked - 1], hi = pr.d[firstBlocked];
            pr.lo = lo; pr.hi = hi;
            if (++pr.stage >= kProbeStages) { pr.state = 1; pr.dist = (lo + hi) * 0.5; pr.doneAt = GetTickCount64(); }
            else ProbeDists(pr);
        }
    }

    void Update() {
        if (!Map()) return;
        const DWORD pid = GetAttachedProcessId();
        if (!pid || !GetWorld()) return;
        const bool here = g_sh->magic == VisShared::kMagic && g_sh->version == VisShared::kVersion && g_sh->pid == pid;
        if (!here && g_injectedPid != pid && GetTickCount64() >= g_nextTry) {
            g_nextTry = GetTickCount64() + 10000;
            if (Inject(pid)) g_injectedPid = pid;
        }
        if (!here) return;
        if (g_sh->dllState == 3 && !g_loggedFail) { g_loggedFail = true; Log_Add(Log_Error, "Line of sight: helper error: %s", g_sh->status); }
        {
            const ULONGLONG now = GetTickCount64();
            for (Probe& pr : g_probes) if (pr.state == 0 && (now - pr.started > 400 || !Active())) { pr.state = -1; pr.doneAt = now; }
            g_probes.erase(std::remove_if(g_probes.begin(), g_probes.end(),
                [&](const Probe& pr) { return pr.state != 0 && now - pr.doneAt > 2000; }), g_probes.end());
        }
        Post();
        Read();
        if (Active() && !g_loggedReady) { g_loggedReady = true; Log_Add(Log_Info, "Line of sight: active (real traces)"); }
    }

    int Verdict(uintptr_t pawn) {
        if (!Active() || GetTickCount64() - g_verdictAt > 300) return -1;
        const auto it = g_verdict.find(pawn);
        return it == g_verdict.end() ? -1 : (it->second ? 1 : 0);
    }
}

void Visibility_Update() {
    los::Update();
    const float t = GetWorldTimeSeconds();
    g_haveTime = t > 1.0f;
    if (g_haveTime) g_time = t;
}

bool Visibility_IsPawnVisible(uintptr_t pawn) {
    if (!g_haveTime || !pawn) return false;
    const int sight = los::Verdict(pawn);
    if (sight >= 0) return sight == 1;
    const uintptr_t mesh = GetMeshComponent(pawn);
    if (!mesh) return false;
    float v = 0.0f;
    return ReadRemote(mesh + OFF(PrimitiveRenderTimeOnScreen), v) && Fresh(v);
}

bool Visibility_LineOfSightActive() { return los::Active(); }

uint32_t Visibility_ProbeStart(const FVector& start, const FVector& dir, double maxDist) {
    if (!los::Active()) return 0;
    const double l = std::sqrt(dir.X * dir.X + dir.Y * dir.Y + dir.Z * dir.Z);
    if (!(l > 1e-6) || !(maxDist > 10.0)) return 0;
    int pending = 0;
    for (const los::Probe& p : los::g_probes) if (p.state == 0) ++pending;
    if (pending >= los::kMaxProbes) return 0;
    los::Probe pr;
    pr.id = los::g_probeNext++;
    if ((los::g_probeNext & 0xFFFFFF) == 0) los::g_probeNext = 1;
    pr.s = start; pr.u = dir * (1.0 / l);
    pr.lo = 0.0; pr.hi = maxDist;
    pr.started = GetTickCount64();
    los::ProbeDists(pr);
    los::g_probes.push_back(pr);
    return pr.id;
}

int Visibility_ProbeResult(uint32_t id, double& dist) {
    for (const los::Probe& p : los::g_probes) {
        if (p.id != id) continue;
        if (p.state == 1) { dist = p.dist; return 1; }
        return p.state;
    }
    return -1;
}
