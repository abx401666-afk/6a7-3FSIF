#include "../include/Targets.hpp"
#include "../include/Visibility.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Log.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr double kHistorySeconds = 0.6;
constexpr double kTeleportUnits  = 500.0;
constexpr double kGraceSeconds   = 0.25;
constexpr double kStaticSeconds  = 2.0;

TargetFrame g_frame;

struct BodyTrack { double downSince = -1.0; double seen = 0.0; };
std::unordered_map<uintptr_t, BodyTrack> g_bodies;
constexpr double kDownConfirmSeconds = 0.6;
constexpr double kStillSpeed         = 40.0;

bool BodyIsDown(uintptr_t pawn, const FVector& capsule, float radius, float halfHeight, double speed, double now) {
    BodyTrack& bt = g_bodies[pawn];
    bt.seen = now;
    SkeletonAimBones b;
    bool down = false;
    if (speed < kStillSpeed && Skeleton_GetAimBones(pawn, b)) {
        if (b.hasHead && b.hasLegs && b.head.Z - b.legs.Z < 45.0) down = true;
        if (b.hasPelvis) {
            const double drift = std::hypot(b.pelvis.X - capsule.X, b.pelvis.Y - capsule.Y);
            const bool pelvisOnFloor = b.pelvis.Z < capsule.Z - halfHeight * 0.6;
            if (drift > (std::max)(70.0, radius * 1.8) || (pelvisOnFloor && down)) down = true;
        }
    }
    if (!down) { bt.downSince = -1.0; return false; }
    if (bt.downSince < 0.0) bt.downSince = now;
    return now - bt.downSince >= kDownConfirmSeconds;
}

std::unordered_map<uintptr_t, uint8_t> g_teamOf;

const int64_t& kPsTeamId        = OFF(PsTeamId);
const int64_t& kPawnPlayerState = OFF(PawnPlayerState);
const int64_t& kDroneOwnerTeam  = OFF(DroneOwnerTeam);
const int64_t& kActorOwner      = OFF(ActorOwner);
bool g_psTeamWorks = false;

bool IsBodycamPlayerState(uintptr_t ps) {
    static std::unordered_map<uintptr_t, bool> cache;
    if (const auto it = cache.find(ps); it != cache.end()) return it->second;
    bool ok = false;
    for (const std::string& n : Skeleton_ClassChain(ps))
        if (n == "bodycamplayerstate") { ok = true; break; }
    if (cache.size() > 512) cache.clear();
    return cache[ps] = ok;
}

uint8_t ReadPsTeam(uintptr_t ps) {
    if (!ps || !IsBodycamPlayerState(ps)) return 0xFF;
    int32_t v = -1;
    if (!ReadRemote(ps + kPsTeamId, v) || v < 0 || v > 64) return 0xFF;
    return static_cast<uint8_t>(v);
}

bool Finite(const FVector& v) { return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z); }

struct PosSample { double t; FVector p; };
struct Track {
    std::deque<PosSample> s;
    FVector vel{};
    double  seen = 0.0;
};
struct CamSample { double t; FCameraCache cam; };

std::unordered_map<uintptr_t, Track> g_tracks;
std::deque<CamSample> g_cams;

std::deque<double> g_frameDts;
double g_lastCamChange = -1.0;
double g_gameFrameDt = 1.0 / 60.0;

bool CamChanged(const FCameraCache& a, const FCameraCache& b) {
    return std::fabs(a.Rotation.Yaw - b.Rotation.Yaw) > 1e-4 || std::fabs(a.Rotation.Pitch - b.Rotation.Pitch) > 1e-4 ||
           (a.Location - b.Location).LengthSq() > 1e-4;
}

double LerpAngle(double a, double b, double u) {
    double d = std::fmod(b - a, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return a + d * u;
}
FVector LerpVec(const FVector& a, const FVector& b, double u) { return a + (b - a) * u; }

void PushPosition(Track& tr, double now, const FVector& p) {
    if (!tr.s.empty()) {
        const PosSample& last = tr.s.back();
        const double dt = now - last.t;
        const FVector d = p - last.p;
        if (d.Length() > kTeleportUnits) {
            tr.s.clear();
            tr.vel = {};
        } else if (dt > 0.004) {
            const FVector inst = d * (1.0 / dt);
            const double a = std::clamp(dt / 0.08, 0.05, 1.0);
            tr.vel = tr.vel + (inst - tr.vel) * a;
        }
    }
    tr.s.push_back({now, p});
    while (tr.s.size() > 2 && now - tr.s.front().t > kHistorySeconds) tr.s.pop_front();
    tr.seen = now;
}

struct StaticInfo { double read = -1.0; float radius = 42.0f, halfHeight = 92.0f; };
std::unordered_map<uintptr_t, StaticInfo> g_static;

struct NameInfo { double read = -1.0; std::string name; };
std::unordered_map<uintptr_t, NameInfo> g_names;

struct Remembered { Target t; double seen = 0.0; };
std::unordered_map<uintptr_t, Remembered> g_remembered;
bool g_resetTrust = false;
bool g_teamsTrusted = false;

std::string ReadFString(uintptr_t addr) {
    uintptr_t data = 0; int32_t count = 0;
    if (!ReadRemote(addr, data) || !ReadRemote(addr + 8, count) || !data || count <= 1 || count > 65) return {};
    std::vector<wchar_t> w(static_cast<size_t>(count), L'\0');
    if (!ReadRemoteBytes(data, w.data(), w.size() * sizeof(wchar_t))) return {};
    std::string out;
    for (int i = 0; i < count - 1; ++i) {
        const wchar_t c = w[static_cast<size_t>(i)];
        if (c < 0x20) return {};
        out += c < 0x80 ? static_cast<char>(c) : '?';
    }
    return out;
}

bool IsGenericBotName(const std::string& n) {
    if (n.empty()) return true;
    std::string l;
    for (char c : n) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return l == "bot" || l == "ai" || l == "player" || l.rfind("bp_", 0) == 0 || l.rfind("ai_", 0) == 0 ||
           l.find("controller") != std::string::npos;
}

std::unordered_map<uintptr_t, int> g_botNumber;
int g_nextBot = 1;

const int64_t& kPsPlatformId = OFF(PsPlatformId);

const int64_t& kBotSkinRow = OFF(BotSkinRowName);
struct OperatorInfo { double read = -1.0; std::string name; };
std::unordered_map<uintptr_t, OperatorInfo> g_operators;

std::string PrettyOperator(const std::string& row) {
    std::string r = row;
    static const char* const kDrop[] = { "skin_", "char_", "character_", "operator_", "bot_", "ai_", "outfit_", "sk_", "bp_", "da_", "dt_" };
    for (bool again = true; again;) {
        again = false;
        std::string l = r;
        for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* d : kDrop) {
            const size_t n = std::strlen(d);
            if (l.compare(0, n, d) == 0) { r = r.substr(n); again = true; break; }
        }
    }
    std::string out;
    for (size_t i = 0; i < r.size(); ++i) {
        const char c = r[i];
        if (c == '_' || c == '-' || c == ' ') { if (!out.empty() && out.back() != ' ') out += ' '; continue; }
        if (std::isupper(static_cast<unsigned char>(c)) && i > 0 && std::islower(static_cast<unsigned char>(r[i - 1])) && !out.empty() && out.back() != ' ') out += ' ';
        out += c;
    }
    while (!out.empty() && (std::isdigit(static_cast<unsigned char>(out.back())) || out.back() == ' ')) out.pop_back();
    for (size_t i = 0; i < out.size(); ++i)
        if (i == 0 || out[i - 1] == ' ') out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
    return out;
}

struct BotSight { double read = -1.0; float halfAngle = -1.0f, radius = -1.0f; };
std::unordered_map<uintptr_t, BotSight> g_botSight;
bool g_sightWorks = false;

int BotSightVerdict(uintptr_t pawn, uintptr_t local, const FVector& botPos, const FVector& localPos, double botYaw, double now) {
    if (!pawn || !local) return -1;
    struct { uintptr_t data; int32_t num, max; } arr{};
    if (!ReadRemote(pawn + OFF(BotSightActors), arr) || arr.num < 0 || arr.num > 64 || (arr.num && !arr.data)) return -1;
    if (arr.num > 0) {
        uintptr_t items[64] = {};
        if (ReadRemoteBytes(arr.data, items, static_cast<size_t>(arr.num) * 8))
            for (int i = 0; i < arr.num; ++i) if (items[i] == local) {
                if (!g_sightWorks) Log_Add(Log_Info, "Visibility: bot line-of-sight list works, using it for bots");
                g_sightWorks = true; return 1;
            }
    }
    if (!g_sightWorks) return -1;
    BotSight& bs = g_botSight[pawn];
    if (bs.read < 0.0 || now - bs.read > 5.0) {
        bs.read = now; bs.halfAngle = -1.0f; bs.radius = -1.0f;
        uintptr_t perc = 0;
        struct { uintptr_t data; int32_t num, max; } cfgs{};
        if (ReadRemote(pawn + OFF(BotAIPerception), perc) && perc && ReadRemote(perc + OFF(PerceptionSensesConfig), cfgs) &&
            cfgs.num > 0 && cfgs.num <= 8 && cfgs.data) {
            for (int i = 0; i < cfgs.num; ++i) {
                uintptr_t c = 0;
                if (!ReadRemote(cfgs.data + i * 8ull, c) || !c) continue;
                bool sight = false;
                for (const std::string& n : Skeleton_ClassChain(c)) if (n.find("aisenseconfig_sight") != std::string::npos) { sight = true; break; }
                if (!sight) continue;
                float a = 0.0f, r = 0.0f;
                if (ReadRemote(c + OFF(SightConfigPeripheral), a) && ReadRemote(c + OFF(SightConfigRadius), r) &&
                    std::isfinite(a) && std::isfinite(r) && a > 1.0f && a <= 180.0f && r > 100.0f) { bs.halfAngle = a; bs.radius = r; }
                break;
            }
        }
    }
    if (bs.halfAngle < 0.0f) return -1;
    const FVector d = localPos - botPos;
    const double dist = d.Length();
    if (dist < 1.0 || dist > bs.radius * 0.9) return -1;
    const double yaw = DegToRad(botYaw);
    const double cosA = (d.X * std::cos(yaw) + d.Y * std::sin(yaw)) / dist;
    const double ang = std::acos(std::clamp(cosA, -1.0, 1.0)) * 57.29577951308232;
    if (ang > bs.halfAngle - 10.0) return -1;
    return 0;
}

std::string BotOperator(uintptr_t pawn, double now) {
    OperatorInfo& o = g_operators[pawn];
    if (o.read >= 0.0 && now - o.read < 5.0) return o.name;
    o.read = now;
    o.name.clear();
    uint32_t idx = 0;
    if (!ReadRemote(pawn + kBotSkinRow, idx) || idx == 0) return o.name;
    const std::string row = Skeleton_FName(idx);
    if (row.empty() || row.size() > 64 || row == "None" || row == "none") return o.name;
    for (char c : row) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) return o.name;
    o.name = PrettyOperator(row);
    if (o.name.size() > 20) o.name.resize(20);
    static std::unordered_set<std::string> logged;
    if (logged.insert(row).second) Log_Add(Log_Info, "Targets: bot operator row \"%s\" -> \"%s\"", row.c_str(), o.name.c_str());
    return o.name;
}

std::string NameFor(uintptr_t state, uintptr_t pawn, bool bot, double now) {
    std::string name;
    if (state) {
        NameInfo& n = g_names[state];
        if (n.read < 0.0 || now - n.read > kStaticSeconds) {
            n.name = GetPlayerName(state);
            if (bot && IsGenericBotName(n.name)) {
                const std::string alt = ReadFString(state + kPsPlatformId);
                if (!IsGenericBotName(alt) && alt.size() < 24 && alt.find_first_not_of("0123456789") != std::string::npos)
                    n.name = alt;
            }
            n.read = now;
        }
        name = n.name;
    }
    if (bot && IsGenericBotName(name)) {
        const std::string op = BotOperator(pawn, now);
        if (!op.empty()) {
            std::string l = op; for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (l.rfind("operator ", 0) == 0) return "Bot " + op.substr(9);
            return "Bot " + op;
        }
        auto it = g_botNumber.find(pawn);
        if (it == g_botNumber.end()) it = g_botNumber.emplace(pawn, g_nextBot++).first;
        name = "Bot " + std::to_string(it->second);
    }
    return name;
}

void Prune(double now) {
    for (auto it = g_tracks.begin(); it != g_tracks.end();)
        it = (now - it->second.seen > 1.0) ? g_tracks.erase(it) : std::next(it);
    for (auto it = g_bodies.begin(); it != g_bodies.end();)
        it = (now - it->second.seen > 5.0) ? g_bodies.erase(it) : std::next(it);
    for (auto it = g_static.begin(); it != g_static.end();)
        it = (now - it->second.read > kStaticSeconds * 2) ? g_static.erase(it) : std::next(it);
    if (g_names.size() > 256) g_names.clear();
}

}


double Targets_Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool Targets_CameraUsable(const FCameraCache& c) {
    return std::isfinite(c.FOV) && c.FOV > 1.0f && c.FOV < 179.0f && Finite(c.Location) &&
           std::isfinite(c.Rotation.Pitch) && std::isfinite(c.Rotation.Yaw) && std::isfinite(c.Rotation.Roll);
}

const TargetFrame& Targets_Get() { return g_frame; }
bool Targets_TeamsTrusted() { return g_teamsTrusted; }

void Targets_Update() {
    TargetFrame f;
    f.localPawn = GetLocalPawn();

    bool prematch = false;
    {
        static uint32_t lastPhase = 0xFFFFFFFFu;
        uint32_t phase = 0;
        const uintptr_t gs = GetGameState();
        if (gs) ReadRemote(gs + OFF(GameStateCurrentPhase), phase);
        const std::string pn = phase ? Skeleton_FName(phase) : std::string();
        prematch = pn.find("warmup") != std::string::npos || pn.find("waiting") != std::string::npos ||
                   pn.find("prematch") != std::string::npos || pn.find("pregame") != std::string::npos;
        if (phase != lastPhase) {
            if (lastPhase != 0xFFFFFFFFu) {
                g_teamOf.clear(); g_remembered.clear(); g_tracks.clear(); g_bodies.clear(); g_static.clear();
                g_resetTrust = true;
            }
            lastPhase = phase;
            Log_Add(Log_Info, "Match phase: %s%s", pn.empty() ? "(none)" : pn.c_str(), prematch ? " (prematch)" : "");
        }
    }
    const uint8_t psLocal = ReadPsTeam(GetLocalPlayerState());
    if (psLocal != 0xFF && !g_psTeamWorks) g_teamOf.clear();
    g_psTeamWorks = psLocal != 0xFF;
    {
        static int lastSource = -1;
        if (lastSource != (g_psTeamWorks ? 1 : 0)) {
            lastSource = g_psTeamWorks ? 1 : 0;
            Log_Add(Log_Info, "Team source: %s", g_psTeamWorks ? "PlayerState TeamID" : "pawn byte (fallback)");
        }
    }
    f.localTeam = g_psTeamWorks ? psLocal : GetLocalTeam();

    std::unordered_set<uintptr_t> decided;

    for (const PawnEntry& e : GetCharacterPawns()) {
        const uintptr_t pawn = e.pawn;
        if (!pawn) continue;
        bool ghost = false;
        {
            uint8_t fl[2] = {};
            uintptr_t ctrl = 0, ps = 0;
            if (ReadRemoteBytes(pawn + OFF(ActorHiddenByte), fl, 1) && ReadRemoteBytes(pawn + OFF(ActorTearOffByte), fl + 1, 1) && ((fl[0] & (1u << OFF(ActorHiddenBit))) || (fl[1] & (1u << OFF(ActorTearOffBit))))) ghost = true;
            ReadRemote(pawn + OFF(PawnController), ctrl);
            ReadRemote(pawn + kPawnPlayerState, ps);
            if (!ctrl && !ps && !e.playerState) ghost = true;
            if (!ghost) {
                uintptr_t back = 0;
                const bool viaCtrl = ctrl && ReadRemote(ctrl + OFF(ControllerPawn), back) && back == pawn;
                back = 0;
                const bool viaPs = ps && ReadRemote(ps + OFF(PlayerStatePawn), back) && back == pawn;
                back = 0;
                const bool viaEntry = e.playerState && ReadRemote(e.playerState + OFF(PlayerStatePawn), back) && back == pawn;
                if (!viaCtrl && !viaPs && !viaEntry) ghost = true;
            }
        }
        if (pawn == f.localPawn || ghost || IsLikelyDronePawn(pawn) || IsPlayerDead(pawn)) {
            decided.insert(pawn);
            g_remembered.erase(pawn);
            continue;
        }

        const double now = Targets_Now();

        Target t;
        t.pawn = pawn;
        t.state = e.playerState ? e.playerState : ReadPtr(pawn, kPawnPlayerState);
        t.bot = IsBotPawn(pawn);
        t.team = g_psTeamWorks ? ReadPsTeam(t.state) : GetPawnTeam(pawn);
        if (t.team != 0xFF) { if (!prematch) g_teamOf[pawn] = t.team; }
        else if (const auto known = g_teamOf.find(pawn); !prematch && known != g_teamOf.end()) t.team = known->second;
        t.friendly = f.localTeam != 0xFF && t.team != 0xFF && t.team == f.localTeam;

        StaticInfo& si = g_static[pawn];
        if (si.read < 0.0 || now - si.read > kStaticSeconds) {
            GetCapsuleDimensions(pawn, si.radius, si.halfHeight);
            si.read = now;
        }
        t.radius = si.radius;
        t.halfHeight = si.halfHeight;

        const FVector p = GetActorLocation(pawn);
        if (!Finite(p) || p.LengthSq() < 1.0) continue;
        const auto trIt = g_tracks.find(pawn);
        const double speed = trIt != g_tracks.end() && !trIt->second.s.empty() &&
                             (p - trIt->second.s.back().p).Length() < kTeleportUnits
                             ? std::hypot(trIt->second.vel.X, trIt->second.vel.Y) : 0.0;
        if (BodyIsDown(pawn, p, t.radius, t.halfHeight, speed, now)) {
            decided.insert(pawn);
            g_remembered.erase(pawn);
            continue;
        }
        t.pos = p;
        t.yaw = GetActorRotation(pawn).Yaw;
        if (!std::isfinite(t.yaw)) t.yaw = 0.0;
        GetPlayerHealthLive(pawn, t.health, t.maxHealth);
        if (!(t.maxHealth >= 1.0)) t.maxHealth = 100.0;
        t.visible = Visibility_IsPawnVisible(pawn);
        if (t.bot && !Visibility_LineOfSightActive()) {
            static double lpAt = -1.0; static FVector lp{};
            if (lpAt != now) { lpAt = now; lp = f.localPawn ? GetActorLocation(f.localPawn) : FVector{}; }
            const int v = BotSightVerdict(pawn, f.localPawn, p, lp, t.yaw, now);
            if (v == 1) t.visible = true;
            else if (v == 0) t.visible = false;
        }
        t.name = NameFor(t.state, pawn, t.bot, now);

        Track& tr = g_tracks[pawn];
        PushPosition(tr, now, p);
        t.vel = tr.vel;

        decided.insert(pawn);
        g_remembered[pawn] = { t, now };
        f.targets.push_back(std::move(t));
    }

    {
        static bool trusted = false;
        if (g_resetTrust) { trusted = false; g_resetTrust = false; }
        bool anyCharacter = false, anyOtherTeam = false;
        for (const Target& t : f.targets) {
            anyCharacter = true;
            if (f.localTeam != 0xFF && t.team != 0xFF && t.team != f.localTeam) anyOtherTeam = true;
        }
        if (!anyCharacter) { trusted = false; g_teamOf.clear(); g_botNumber.clear(); g_operators.clear(); g_nextBot = 1; }
        if (anyOtherTeam) trusted = true;
        if (prematch) trusted = anyOtherTeam;
        g_teamsTrusted = trusted;
        if (!trusted) for (Target& t : f.targets) t.friendly = false;
    }

    {
        const double now = Targets_Now();
        for (auto it = g_remembered.begin(); it != g_remembered.end();) {
            if (now - it->second.seen > kGraceSeconds) { it = g_remembered.erase(it); continue; }
            if (!decided.count(it->first)) {
                Target t = it->second.t;
                t.stale = true;
                f.targets.push_back(std::move(t));
            }
            ++it;
        }
    }

    if (!g_teamsTrusted) for (Target& t : f.targets) t.friendly = false;

    for (const uintptr_t d : GetDronePawns()) {
        if (!d || d == f.localPawn) continue;
        const FVector p = GetActorLocation(d);
        if (!Finite(p) || p.LengthSq() < 1.0) continue;
        Target t;
        t.pawn = d;
        t.drone = true;
        t.radius = 30.0f;
        t.halfHeight = 30.0f;
        t.pos = p;
        t.yaw = GetActorRotation(d).Yaw;
        {
            int32_t ownerTeam = -1;
            uint8_t team = 0xFF;
            if (ReadRemote(d + kDroneOwnerTeam, ownerTeam) && ownerTeam >= 0 && ownerTeam <= 64) team = static_cast<uint8_t>(ownerTeam);
            if (team == 0xFF) team = ReadPsTeam(ReadPtr(d, kPawnPlayerState));
            const uintptr_t owner = ReadPtr(d, kActorOwner);
            t.team = team;
            t.friendly = (owner && (owner == f.localPawn || owner == GetLocalPlayerController())) ||
                         (g_teamsTrusted && g_psTeamWorks && team != 0xFF && team == f.localTeam);
        }
        Track& tr = g_tracks[d];
        PushPosition(tr, Targets_Now(), p);
        t.vel = tr.vel;
        f.targets.push_back(std::move(t));
    }

    f.cam = GetCameraCache();
    f.camValid = Targets_CameraUsable(f.cam);
    f.time = Targets_Now();
    if (f.camValid) {
        if (!g_cams.empty() && CamChanged(g_cams.back().cam, f.cam)) {
            if (g_lastCamChange > 0.0) {
                const double dt = f.time - g_lastCamChange;
                if (dt > 0.004 && dt < 0.15) {
                    g_frameDts.push_back(dt);
                    if (g_frameDts.size() > 41) g_frameDts.pop_front();
                    if (g_frameDts.size() >= 9) {
                        std::vector<double> v(g_frameDts.begin(), g_frameDts.end());
                        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                        g_gameFrameDt = v[v.size() / 2];
                    }
                }
            }
            g_lastCamChange = f.time;
        }
        g_cams.push_back({f.time, f.cam});
        while (g_cams.size() > 2 && f.time - g_cams.front().t > kHistorySeconds) g_cams.pop_front();
        for (Target& t : f.targets) {
            t.distM = static_cast<float>((t.pos - f.cam.Location).Length() / 100.0);
            if (t.visible) {
                float x = 0.0f, y = 0.0f;
                const bool inView = WorldToScreen(t.pos, f.cam.Location, f.cam.Rotation, f.cam.FOV, 1920.0f, 1080.0f, x, y) &&
                                    x > -60.0f && x < 1980.0f && y > -120.0f && y < 1200.0f;
                if (!inView) t.visible = false;
            }
        }
    }

    Prune(f.time);
    g_frame = std::move(f);

}

double Targets_GameFrameTime() { return g_gameFrameDt; }

FVector Targets_PositionAt(uintptr_t pawn, double t, const FVector& fallback) {
    const auto it = g_tracks.find(pawn);
    if (it == g_tracks.end() || it->second.s.empty()) return fallback;
    const auto& s = it->second.s;
    if (t <= s.front().t) return s.front().p;
    if (t >= s.back().t) return s.back().p;
    for (size_t i = 1; i < s.size(); ++i) {
        if (t > s[i].t) continue;
        const PosSample& a = s[i - 1];
        const PosSample& b = s[i];
        const double span = b.t - a.t;
        const double u = span > 1e-6 ? (t - a.t) / span : 1.0;
        return LerpVec(a.p, b.p, u);
    }
    return s.back().p;
}

FCameraCache Targets_CameraAt(double t, const FCameraCache& fallback) {
    if (g_cams.empty()) return fallback;
    if (t <= g_cams.front().t) return g_cams.front().cam;
    if (t >= g_cams.back().t) return g_cams.back().cam;
    for (size_t i = 1; i < g_cams.size(); ++i) {
        if (t > g_cams[i].t) continue;
        const CamSample& a = g_cams[i - 1];
        const CamSample& b = g_cams[i];
        const double span = b.t - a.t;
        const double u = span > 1e-6 ? (t - a.t) / span : 1.0;
        if ((b.cam.Location - a.cam.Location).Length() > kTeleportUnits) return u < 0.5 ? a.cam : b.cam;
        FCameraCache o = b.cam;
        o.Location = LerpVec(a.cam.Location, b.cam.Location, u);
        o.Rotation.Pitch = LerpAngle(a.cam.Rotation.Pitch, b.cam.Rotation.Pitch, u);
        o.Rotation.Yaw   = LerpAngle(a.cam.Rotation.Yaw,   b.cam.Rotation.Yaw,   u);
        o.Rotation.Roll  = LerpAngle(a.cam.Rotation.Roll,  b.cam.Rotation.Roll,  u);
        o.FOV = static_cast<float>(a.cam.FOV + (b.cam.FOV - a.cam.FOV) * u);
        return o;
    }
    return g_cams.back().cam;
}
