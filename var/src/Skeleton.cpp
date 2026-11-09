#include "../include/Skeleton.hpp"
#include "../include/Memory.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct ArrHdr { uintptr_t data = 0; int32_t count = 0; int32_t max = 0; };
static_assert(sizeof(ArrHdr) == 0x10);

constexpr ptrdiff_t kCompScanBegin = 0x300;
constexpr ptrdiff_t kCompScanEnd   = 0xC00;
constexpr ptrdiff_t kAssetScanEnd  = 0x1000;
const int64_t& kCachedCSOff     = BodycamOffsets::SkeletalComponentTransforms;
constexpr int32_t   kMinBones = 10, kMaxBones = 2048;

bool PlausiblePtr(uintptr_t p) {
    return p >= 0x10000 && p < 0x00007FFFFFFFFFFFull && (p & 7) == 0;
}

bool HdrOk(const ArrHdr& h, int32_t n) {
    return h.count == n && h.max >= h.count && h.max <= kMaxBones * 4 && PlausiblePtr(h.data);
}

void ReadWindow(uintptr_t addr, std::vector<uint8_t>& buf, size_t size) {
    buf.assign(size, 0);
    if (ReadRemoteBytes(addr, buf.data(), size)) return;
    for (size_t o = 0; o < size; o += 0x100) {
        const size_t n = (std::min)(static_cast<size_t>(0x100), size - o);
        if (!ReadRemoteBytes(addr + o, buf.data() + o, n)) std::memset(buf.data() + o, 0, n);
    }
}

template <typename T>
T At(const std::vector<uint8_t>& buf, size_t off) {
    T v{};
    if (off + sizeof(T) <= buf.size()) std::memcpy(&v, buf.data() + off, sizeof(T));
    return v;
}

DWORD g_cachePid = 0;

int g_blocksOff = -1;
ULONGLONG g_poolRetry = 0;
std::unordered_map<uint32_t, std::string> g_names;

bool DecodeName(int blocksOff, uint32_t idx, std::string& out) {
    const uintptr_t base = GetRemoteModuleBase();
    if (!base) return false;
    const uint32_t block = idx >> 16;
    if (block >= 8192) return false;
    uintptr_t blk = 0;
    if (!ReadRemote(base + Offsets::GNames + blocksOff + block * 8ull, blk) || !PlausiblePtr(blk)) return false;
    const uintptr_t entry = blk + (idx & 0xFFFFu) * 2ull;
    uint16_t hdr = 0;
    if (!ReadRemote(entry, hdr)) return false;
    const int len = hdr >> 6;
    if (len <= 0 || len > 1024) return false;
    if (hdr & 1) {
        std::vector<wchar_t> w(static_cast<size_t>(len));
        if (!ReadRemoteBytes(entry + 2, w.data(), w.size() * sizeof(wchar_t))) return false;
        out = WideToUtf8(w.data(), len);
    } else {
        out.assign(static_cast<size_t>(len), '\0');
        if (!ReadRemoteBytes(entry + 2, out.data(), out.size())) return false;
    }
    return true;
}

bool NamePoolReady() {
    if (g_blocksOff >= 0) return true;
    const ULONGLONG now = GetTickCount64();
    if (now < g_poolRetry) return false;
    g_poolRetry = now + 2000;
    for (const int off : {0x10, 0x08}) {
        std::string s;
        if (DecodeName(off, 0, s) && s == "None") { g_blocksOff = off; return true; }
    }
    return false;
}

const std::string& NameOf(uint32_t idx) {
    static const std::string kEmpty;
    const auto it = g_names.find(idx);
    if (it != g_names.end()) return it->second;
    if (!NamePoolReady()) return kEmpty;
    std::string s;
    if (!DecodeName(g_blocksOff, idx, s)) return kEmpty;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (g_names.size() > 65536) g_names.clear();
    return g_names.emplace(idx, std::move(s)).first->second;
}

std::string ClassNameOf(uintptr_t obj) {
    const uintptr_t cls = ReadPtr(obj, OFF(ObjectClass));
    if (!PlausiblePtr(cls)) return {};
    uint32_t idx = 0;
    if (!ReadRemote(cls + OFF(ObjectName), idx)) return {};
    return NameOf(idx);
}

ptrdiff_t g_assetOff = 0;
std::unordered_set<uintptr_t> g_knownAssets;
std::unordered_map<uintptr_t, ULONGLONG> g_assetScanRetry;

bool IsSkeletalMeshAsset(uintptr_t obj) {
    if (!PlausiblePtr(obj)) return false;
    if (g_knownAssets.count(obj)) return true;
    if (ClassNameOf(obj) != "skeletalmesh") return false;
    g_knownAssets.insert(obj);
    return true;
}

uintptr_t GetMeshAsset(uintptr_t mesh) {
    if (g_assetOff) {
        const uintptr_t a = ReadPtr(mesh, g_assetOff);
        if (IsSkeletalMeshAsset(a)) return a;
    }
    const ULONGLONG now = GetTickCount64();
    ULONGLONG& retry = g_assetScanRetry[mesh];
    if (now < retry) return 0;
    retry = now + 2000;
    if (g_assetScanRetry.size() > 512) g_assetScanRetry.clear();

    std::vector<uint8_t> win;
    ReadWindow(mesh + kCompScanBegin, win, kCompScanEnd - kCompScanBegin);
    for (size_t o = 0; o + 8 <= win.size(); o += 8) {
        const uintptr_t p = At<uintptr_t>(win, o);
        if (p == mesh || !PlausiblePtr(p)) continue;
        if (IsSkeletalMeshAsset(p)) {
            g_assetOff = kCompScanBegin + static_cast<ptrdiff_t>(o);
            return p;
        }
    }
    return 0;
}

struct SkelInfo {
    bool ok = false;
    ULONGLONG retry = 0;
    int32_t numBones = 0;
    bool named = false;
    int head = -1, footL = -1, footR = -1, pelvis = -1;
    int neck = -1, chest = -1, calfL = -1, calfR = -1;
    std::vector<std::pair<int, int>> links;
};
std::unordered_map<uintptr_t, SkelInfo> g_skels;

const std::unordered_set<std::string>& MajorBones() {
    static const std::unordered_set<std::string> s = {
        "pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05",
        "neck_01", "neck_02", "head",
        "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
        "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
        "thigh_l", "calf_l", "foot_l", "ball_l",
        "thigh_r", "calf_r", "foot_r", "ball_r",
        "hips", "spine", "spine1", "spine2", "neck",
        "leftshoulder", "leftarm", "leftforearm", "lefthand",
        "rightshoulder", "rightarm", "rightforearm", "righthand",
        "leftupleg", "leftleg", "leftfoot", "lefttoebase",
        "rightupleg", "rightleg", "rightfoot", "righttoebase",
    };
    return s;
}

bool ResolveSkeleton(uintptr_t asset, SkelInfo& out) {
    std::vector<uint8_t> win;
    ReadWindow(asset, win, kAssetScanEnd);

    struct Layout { size_t stride, parentOff; };
    static constexpr Layout kLayouts[] = { {0xC, 8}, {0x10, 0xC}, {0x10, 8} };

    std::vector<int32_t> bestParents;
    std::vector<uint32_t> bestNames;
    for (size_t o = 0x28; o + 0x20 <= win.size(); o += 8) {
        const ArrHdr h = At<ArrHdr>(win, o);
        if (h.count < kMinBones || h.count > kMaxBones || !HdrOk(h, h.count)) continue;
        if (h.count <= static_cast<int32_t>(bestParents.size())) continue;
        const ArrHdr pose = At<ArrHdr>(win, o + 0x10);
        if (!HdrOk(pose, h.count)) continue;

        for (const Layout& L : kLayouts) {
            std::vector<uint8_t> raw(static_cast<size_t>(h.count) * L.stride);
            if (!ReadRemoteBytes(h.data, raw.data(), raw.size())) continue;
            std::vector<int32_t> parents(static_cast<size_t>(h.count));
            std::vector<uint32_t> names(static_cast<size_t>(h.count));
            bool valid = true;
            for (int32_t i = 0; i < h.count && valid; ++i) {
                const size_t e = static_cast<size_t>(i) * L.stride;
                parents[i] = At<int32_t>(raw, e + L.parentOff);
                names[i] = At<uint32_t>(raw, e);
                valid = (i == 0) ? parents[i] == -1 : (parents[i] >= 0 && parents[i] < i);
            }
            if (!valid) continue;
            bestParents = std::move(parents);
            bestNames = std::move(names);
            break;
        }
    }
    if (bestParents.empty()) return false;

    const int32_t n = static_cast<int32_t>(bestParents.size());
    out.numBones = n;

    std::vector<std::string> names(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        std::string s = NameOf(bestNames[i]);
        const size_t colon = s.rfind(':');
        if (colon != std::string::npos) s.erase(0, colon + 1);
        names[i] = std::move(s);
    }

    const auto& major = MajorBones();
    auto isMajor = [&](int i) { return major.count(names[i]) != 0; };
    for (int32_t i = 0; i < n; ++i) {
        const std::string& s = names[i];
        if (s == "head") out.head = i;
        else if (s == "pelvis" || s == "hips") out.pelvis = i;
        else if (s == "foot_l" || s == "leftfoot") out.footL = i;
        else if (s == "foot_r" || s == "rightfoot") out.footR = i;
        else if (s == "neck_01" || s == "neck") out.neck = i;
        else if (s == "spine_03" || s == "spine2") out.chest = i;
        else if (s == "calf_l" || s == "leftleg") out.calfL = i;
        else if (s == "calf_r" || s == "rightleg") out.calfR = i;
        if (!isMajor(i)) continue;
        int p = bestParents[i];
        while (p >= 0 && !isMajor(p)) p = bestParents[p];
        if (p >= 0) out.links.emplace_back(p, i);
    }
    out.named = out.links.size() >= 10 && out.head >= 0;

    if (!out.named) {
        out.links.clear();
        for (int32_t i = 1; i < n; ++i) {
            const int p = bestParents[i];
            if (p <= 0) continue;
            const std::string& s = names[i];
            if (s.find("ik_") == 0 || s.find("vb ") == 0 || s.find("twist") != std::string::npos ||
                s.find("corrective") != std::string::npos) continue;
            out.links.emplace_back(p, i);
        }
    }
    return !out.links.empty();
}

const SkelInfo* GetSkeleton(uintptr_t asset) {
    SkelInfo& si = g_skels[asset];
    if (si.ok) return &si;
    const ULONGLONG now = GetTickCount64();
    if (now < si.retry) return nullptr;
    si = SkelInfo{};
    si.ok = ResolveSkeleton(asset, si);
    if (!si.ok) { si.retry = now + 3000; return nullptr; }
    return &si;
}

int g_poseMode = 0;
ptrdiff_t g_poseOff = 0;
ULONGLONG g_poseRetry = 0;

bool SaneTransform(const FTransform& t) {
    const double q = t.Rotation.X * t.Rotation.X + t.Rotation.Y * t.Rotation.Y +
                     t.Rotation.Z * t.Rotation.Z + t.Rotation.W * t.Rotation.W;
    auto fin = [](const FVector& v) { return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z); };
    return std::isfinite(q) && q > 0.8 && q < 1.2 && fin(t.Translation) && fin(t.Scale3D) &&
           t.Translation.LengthSq() < 1000.0 * 1000.0 &&
           std::fabs(t.Scale3D.X) > 0.01 && std::fabs(t.Scale3D.X) < 100.0 &&
           std::fabs(t.Scale3D.Z) > 0.01 && std::fabs(t.Scale3D.Z) < 100.0;
}

bool ReadPose(uintptr_t data, int32_t n, std::vector<FTransform>& out) {
    out.resize(static_cast<size_t>(n));
    return ReadRemoteBytes(data, out.data(), out.size() * sizeof(FTransform));
}

bool LooksComponentSpace(const std::vector<FTransform>& pose, const SkelInfo& sk) {
    for (int i = 0; i < (std::min)(static_cast<int>(pose.size()), 8); ++i)
        if (!SaneTransform(pose[i])) return false;
    if (sk.head < 0) return true;
    const int foot = sk.footL >= 0 ? sk.footL : sk.footR;
    if (foot < 0 || foot >= static_cast<int>(pose.size())) return true;
    return (pose[sk.head].Translation - pose[foot].Translation).Length() > 80.0;
}

int g_poseFailures = 0;

void LearnPoseBuffer(uintptr_t mesh, const SkelInfo& sk) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_poseRetry) return;
    g_poseRetry = now + 1000;

    std::vector<uint8_t> win;
    ReadWindow(mesh + kCompScanBegin, win, kCompScanEnd - kCompScanBegin);
    std::vector<FTransform> pose;
    auto live = [&](const ArrHdr& h) {
        return HdrOk(h, sk.numBones) && ReadPose(h.data, sk.numBones, pose) && LooksComponentSpace(pose, sk);
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t o = 0; o + 0x28 <= win.size(); o += 8) {
            const ptrdiff_t abs = kCompScanBegin + static_cast<ptrdiff_t>(o);
            if (abs >= kCachedCSOff - 0x20 && abs <= kCachedCSOff + 0x10) continue;
            const ArrHdr a = At<ArrHdr>(win, o), b = At<ArrHdr>(win, o + 0x10);
            if (a.data == b.data) continue;
            const bool ok = pass == 0 ? (live(a) && live(b))
                                      : (live(a) && b.count == 0 && b.data == 0);
            if (!ok) continue;
            g_poseMode = 1;
            g_poseOff = abs;
            g_poseFailures = 0;
            return;
        }
    }

    const ArrHdr c = At<ArrHdr>(win, static_cast<size_t>(kCachedCSOff - kCompScanBegin));
    if (g_poseMode != 2 && live(c)) {
        g_poseMode = 2;
        g_poseOff = kCachedCSOff;
        g_poseFailures = 0;
    }
    if (g_poseMode == 2) g_poseRetry = now + 3000;
}

bool ReadLivePose(uintptr_t mesh, const SkelInfo& sk, std::vector<FTransform>& pose) {
    if (g_poseMode != 1) LearnPoseBuffer(mesh, sk);
    if (g_poseMode == 0) return false;

    uintptr_t data = 0;
    if (g_poseMode == 1) {
        struct { ArrHdr arr[2]; int32_t editIdx, readIdx; } blk{};
        if (ReadRemote(mesh + g_poseOff, blk)) {
            const int idx = (blk.readIdx == 0 || blk.readIdx == 1) ? blk.readIdx : 0;
            if (HdrOk(blk.arr[idx], sk.numBones)) data = blk.arr[idx].data;
            else if (HdrOk(blk.arr[idx ^ 1], sk.numBones)) data = blk.arr[idx ^ 1].data;
        }
    } else {
        ArrHdr h{};
        if (ReadRemote(mesh + g_poseOff, h) && HdrOk(h, sk.numBones)) data = h.data;
    }
    if (data && ReadPose(data, sk.numBones, pose) && SaneTransform(pose[0])) {
        g_poseFailures = 0;
        return true;
    }
    if (++g_poseFailures > 60) { g_poseMode = 0; g_poseFailures = 0; }
    return false;
}

FVector RotateByRotator(const FVector& v, const FRotator& r) {
    const double sp = std::sin(DegToRad(r.Pitch)), cp = std::cos(DegToRad(r.Pitch));
    const double sy = std::sin(DegToRad(r.Yaw)),   cy = std::cos(DegToRad(r.Yaw));
    const double sr = std::sin(DegToRad(r.Roll)),  cr = std::cos(DegToRad(r.Roll));
    const FVector ax{ cp * cy, cp * sy, sp };
    const FVector ay{ sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp };
    const FVector az{ -(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp };
    return ax * v.X + ay * v.Y + az * v.Z;
}

struct SceneRel { FVector loc; FRotator rot; FVector scale; };
static_assert(sizeof(SceneRel) == 0x48);

struct HeldPose { std::vector<std::pair<FVector, FVector>> lines; FVector root{}; ULONGLONG ms = 0; };
std::unordered_map<uintptr_t, HeldPose> g_held;
constexpr ULONGLONG kHoldMs = 300;

struct PoseCacheEntry { double t = -1.0; const SkelInfo* sk = nullptr; bool ok = false; std::vector<FVector> world; };
std::unordered_map<uintptr_t, PoseCacheEntry> g_poseCache;

void ResetIfNewProcess() {
    const DWORD pid = GetAttachedProcessId();
    if (pid == g_cachePid) return;
    g_cachePid = pid;
    g_blocksOff = -1; g_poolRetry = 0; g_names.clear();
    g_assetOff = 0; g_knownAssets.clear(); g_assetScanRetry.clear();
    g_skels.clear();
    g_held.clear();
    g_poseCache.clear();
    g_poseMode = 0; g_poseOff = 0; g_poseRetry = 0; g_poseFailures = 0;
}

}

namespace {

bool ReadWorldPose(uintptr_t pawn, const SkelInfo*& skOut, std::vector<FVector>& world) {
    ResetIfNewProcess();
    if (!pawn) return false;

    const uintptr_t mesh = GetMeshComponent(pawn);
    if (!PlausiblePtr(mesh)) { return false; }

    const uintptr_t asset = GetMeshAsset(mesh);
    if (!asset) {
        return false;
    }
    const SkelInfo* sk = GetSkeleton(asset);
    if (!sk) { return false; }

    static std::vector<FTransform> pose;
    if (!ReadLivePose(mesh, *sk, pose)) {
        return false;
    }

    SceneRel meshRel{};
    if (!ReadRemote(mesh + BodycamOffsets::SceneRelativeLocation, meshRel)) return false;
    if (!std::isfinite(meshRel.scale.X) || std::fabs(meshRel.scale.X) < 0.01) meshRel.scale = {1.0, 1.0, 1.0};

    const uintptr_t root = ReadPtr(pawn, BodycamOffsets::ActorRootComponent);
    SceneRel rootRel{};
    if (!PlausiblePtr(root) || !ReadRemote(root + BodycamOffsets::SceneRelativeLocation, rootRel)) return false;

    const FVector meshWorld = rootRel.loc + RotateByRotator(meshRel.loc, rootRel.rot);
    world.resize(pose.size());
    for (size_t i = 0; i < pose.size(); ++i) {
        const FVector& t = pose[i].Translation;
        const FVector scaled{ t.X * meshRel.scale.X, t.Y * meshRel.scale.Y, t.Z * meshRel.scale.Z };
        world[i] = meshWorld + RotateByRotator(RotateByRotator(scaled, meshRel.rot), rootRel.rot);
    }
    skOut = sk;
    return true;
}

constexpr double kPoseCacheSeconds = 0.004;

double NowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool ReadWorldPoseCached(uintptr_t pawn, const SkelInfo*& sk, const std::vector<FVector>*& world) {
    ResetIfNewProcess();
    const double now = NowSec();
    if (g_poseCache.size() > 256) g_poseCache.clear();
    PoseCacheEntry& e = g_poseCache[pawn];
    if (e.t < 0.0 || now - e.t > kPoseCacheSeconds) {
        e.ok = ReadWorldPose(pawn, e.sk, e.world);
        e.t = now;
    }
    sk = e.sk;
    world = &e.world;
    return e.ok && sk;
}

}

namespace {
    bool NearRoot(const FVector& v, const FVector& root) {
        if (root.LengthSq() < 1.0) return true;
        const double dx = v.X - root.X, dy = v.Y - root.Y, dz = v.Z - root.Z;
        return dx * dx + dy * dy < 250.0 * 250.0 && std::fabs(dz) < 300.0;
    }
}

bool Skeleton_GetPose(uintptr_t pawn, SkeletonPose& out) {
    out.lines.clear();
    const SkelInfo* sk = nullptr;
    const std::vector<FVector>* wp = nullptr;
    const ULONGLONG now = GetTickCount64();
    if (!ReadWorldPoseCached(pawn, sk, wp)) {
        const auto it = g_held.find(pawn);
        if (it == g_held.end() || now - it->second.ms > kHoldMs) return false;
        const FVector root = GetActorLocation(pawn);
        const FVector shift = root.LengthSq() > 1.0 ? root - it->second.root : FVector{};
        if (shift.LengthSq() > 200.0 * 200.0) return false;
        for (const auto& [a, b] : it->second.lines) out.lines.emplace_back(a + shift, b + shift);
        return !out.lines.empty();
    }

    const std::vector<FVector>& world = *wp;
    const FVector root = GetActorLocation(pawn);
    out.lines.reserve(sk->links.size());
    for (const auto& [p, c] : sk->links) {
        const FVector& a = world[static_cast<size_t>(p)];
        const FVector& b = world[static_cast<size_t>(c)];
        const double len = (b - a).Length();
        if (len < 1.5 || len > 150.0) continue;
        if (!NearRoot(a, root) || !NearRoot(b, root)) { out.lines.clear(); break; }
        out.lines.emplace_back(a, b);
    }
    if (out.lines.empty()) {
        const auto it = g_held.find(pawn);
        if (it == g_held.end() || now - it->second.ms > kHoldMs) return false;
        const FVector shift = root.LengthSq() > 1.0 ? root - it->second.root : FVector{};
        if (shift.LengthSq() > 200.0 * 200.0) return false;
        for (const auto& [a, b] : it->second.lines) out.lines.emplace_back(a + shift, b + shift);
        return !out.lines.empty();
    }
    if (!out.lines.empty()) {
        if (g_held.size() > 256) {
            for (auto it = g_held.begin(); it != g_held.end();)
                it = (now - it->second.ms > kHoldMs) ? g_held.erase(it) : std::next(it);
        }
        HeldPose& h = g_held[pawn];
        h.lines = out.lines;
        h.root = root;
        h.ms = now;
    }
    return !out.lines.empty();
}

bool Skeleton_GetAimBones(uintptr_t pawn, SkeletonAimBones& out) {
    out = SkeletonAimBones{};
    const SkelInfo* sk = nullptr;
    const std::vector<FVector>* wp = nullptr;
    if (!ReadWorldPoseCached(pawn, sk, wp) || !sk->named) return false;
    const std::vector<FVector>& world = *wp;
    auto get = [&](int idx, FVector& v) {
        if (idx < 0 || idx >= static_cast<int>(world.size())) return false;
        v = world[static_cast<size_t>(idx)];
        return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
    };
    const FVector root = GetActorLocation(pawn);
    auto nearOk = [&](bool has, const FVector& v) { return has && NearRoot(v, root); };
    out.hasHead   = get(sk->head, out.head);
    out.hasNeck   = get(sk->neck, out.neck);
    out.hasChest  = get(sk->chest, out.chest);
    out.hasPelvis = get(sk->pelvis, out.pelvis);
    FVector l, r;
    const bool hl = get(sk->calfL, l), hr = get(sk->calfR, r);
    out.hasLegs = hl || hr;
    if (hl && hr) out.legs = (l + r) * 0.5; else if (hl) out.legs = l; else if (hr) out.legs = r;
    out.hasHead = nearOk(out.hasHead, out.head);
    out.hasNeck = nearOk(out.hasNeck, out.neck);
    out.hasChest = nearOk(out.hasChest, out.chest);
    out.hasPelvis = nearOk(out.hasPelvis, out.pelvis);
    out.hasLegs = nearOk(out.hasLegs, out.legs);
    return out.hasHead || out.hasChest;
}


std::vector<std::string> Skeleton_ClassChain(uintptr_t obj) {
    static std::unordered_map<uintptr_t, std::vector<std::string>> cache;
    const uintptr_t cls = ReadPtr(obj, OFF(ObjectClass));
    if (!PlausiblePtr(cls)) return {};
    const auto it = cache.find(cls);
    if (it != cache.end()) return it->second;
    std::vector<std::string> chain;
    uintptr_t c = cls;
    for (int depth = 0; depth < 12 && PlausiblePtr(c); ++depth) {
        uint32_t idx = 0;
        if (!ReadRemote(c + OFF(ObjectName), idx)) break;
        const std::string& n = NameOf(idx);
        if (n.empty()) break;
        chain.push_back(n);
        c = ReadPtr(c, OFF(StructSuper));
    }
    if (!chain.empty()) cache[cls] = chain;
    return chain;
}

std::string Skeleton_FName(uint32_t comparisonIndex) { return NameOf(comparisonIndex); }
