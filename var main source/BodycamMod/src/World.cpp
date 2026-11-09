#include "../include/World.hpp"
#include "../include/Memory.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Targets.hpp"
#include "../include/Log.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {
    const int64_t& kCharacterOwner = OFF(ItemCharacterOwner);
    const int64_t& kIsVisible = OFF(ItemIsVisible);
    const int64_t& kReloadDecoy = OFF(WepReloadDecoy);
    const int64_t& kFuseDelay = OFF(GrenadeFuseDelay);
    const int64_t& kIsBlowing = OFF(GrenadeIsBlowing);
    const int64_t& kIsThrown = OFF(GrenadeIsThrown);
    const int64_t& kLaunchSpeed = OFF(GrenadeLaunchSpeed);
    const int64_t& kPsKill = OFF(PsKill);
    const int64_t& kPsDeath = OFF(PsDeath);
    const int64_t& kPsTeam = OFF(PsTeamId);
    const int64_t& kPawnPs = OFF(PawnPlayerState);

    const int64_t& kWepRowName = OFF(ItemWepRowName);

    struct ItemInfo { uintptr_t cls = 0; int kind = Weapon_Other; std::string name; size_t slot = 0; bool fromRow = false; std::string row; uint32_t rowIdx = 0; bool handItem = false; };
    std::unordered_map<uintptr_t, ItemInfo> g_items;
    std::unordered_map<uintptr_t, bool> g_classIsItem;
    std::unordered_map<uintptr_t, HeldItem> g_held;
    std::vector<ThrownGrenade> g_grenades;
    std::unordered_map<uintptr_t, double> g_thrownSeen;
    std::unordered_map<uintptr_t, uintptr_t> g_grenadeHolder;
    uintptr_t g_localGrenade = 0;
    std::vector<uintptr_t> g_scan;
    size_t g_scanIdx = 0;
    ULONGLONG g_lastSlice = 0;
    int g_kills = -1, g_deaths = -1;

    struct Def { const char* cls; const char* name; int kind; };
    const Def kDefs[] = {
        { "ak_47_c", "AK-47", Weapon_Rifle },      { "m4a1_c", "M4A1", Weapon_Rifle },
        { "scar_c", "SCAR", Weapon_Rifle },        { "fal_c", "FAL", Weapon_Rifle },
        { "dragunov_c", "Dragunov", Weapon_Sniper },
        { "uzi_c", "UZI", Weapon_SMG },
        { "glock17_c", "Glock 17", Weapon_Pistol }, { "deagle_c", "Desert Eagle", Weapon_Pistol },
        { "kobra_c", "Kobra", Weapon_Pistol },     { "m19_c", "M19", Weapon_Pistol },
        { "fostech_c", "Fostech", Weapon_Shotgun }, { "shotgun_c", "Shotgun", Weapon_Shotgun },
        { "flash_c", "Flashbang", Weapon_Grenade }, { "g67_c", "G67 Frag", Weapon_Grenade },
        { "mb78_c", "MB78", Weapon_Grenade },
        { "knife_c", "Knife", Weapon_Knife },
        { "bombe_c", "Bomb", Weapon_Other },
    };

    struct RowDef { const char* key; const char* name; int kind; };
    const RowDef kRows[] = {
        { "ka74m", "KA-74M", Weapon_Rifle }, { "ak74m", "KA-74M", Weapon_Rifle },
        { "kaus", "KA-US", Weapon_Rifle }, { "aks74u", "KA-US", Weapon_Rifle }, { "ak74u", "KA-US", Weapon_Rifle },
        { "ka74", "KA-74", Weapon_Rifle }, { "ak74", "KA-74", Weapon_Rifle }, { "ak47", "KA-74", Weapon_Rifle }, { "akm", "KA-74", Weapon_Rifle },
        { "drako", "Drako", Weapon_Rifle }, { "draco", "Drako", Weapon_Rifle },
        { "m161a", "M16-1A", Weapon_Rifle }, { "m16", "M16-1A", Weapon_Rifle },
        { "skrh", "SKR-H", Weapon_Rifle }, { "scar", "SKR-H", Weapon_Rifle },
        { "m4", "M4", Weapon_Rifle }, { "ar15", "M4", Weapon_Rifle },
        { "sg5kf", "SG5-KF", Weapon_SMG }, { "mp5kf", "SG5-KF", Weapon_SMG }, { "mp5sd", "SG5-KF", Weapon_SMG },
        { "sg5k", "SG5-K", Weapon_SMG }, { "mp5k", "SG5-K", Weapon_SMG }, { "mp5", "SG5-K", Weapon_SMG },
        { "sg9x", "SG9-X", Weapon_SMG }, { "mp9", "SG9-X", Weapon_SMG },
        { "veaper", "Veaper", Weapon_SMG }, { "vector", "Veaper", Weapon_SMG }, { "kriss", "Veaper", Weapon_SMG },
        { "uz1", "UZ-1", Weapon_SMG }, { "uzi", "UZ-1", Weapon_SMG },
        { "bk102s", "BK-102S", Weapon_Shotgun }, { "bk102", "BK-102S", Weapon_Shotgun }, { "bk101", "BK-101", Weapon_Shotgun },
        { "r12", "R12", Weapon_Shotgun }, { "m870", "R12", Weapon_Shotgun }, { "remington", "R12", Weapon_Shotgun },
        { "vaiga", "Vaiga", Weapon_Shotgun }, { "saiga", "Vaiga", Weapon_Shotgun }, { "shotgun", "Shotgun", Weapon_Shotgun },
        { "tacrivington", "TAC Rivington", Weapon_Sniper }, { "rivington700", "Rivington 700", Weapon_Sniper },
        { "rivington", "Rivington 700", Weapon_Sniper }, { "m700", "Rivington 700", Weapon_Sniper },
        { "usd", "USD", Weapon_Sniper }, { "svd", "USD", Weapon_Sniper }, { "dragunov", "USD", Weapon_Sniper },
        { "lar", "LAR", Weapon_Sniper },
        { "revolver", "Revolver", Weapon_Pistol },
        { "mlock19", "Mlock19", Weapon_Pistol }, { "mlock", "Mlock19", Weapon_Pistol }, { "glock", "Mlock19", Weapon_Pistol },
        { "cr75", "CR-75", Weapon_Pistol }, { "cz75", "CR-75", Weapon_Pistol },
        { "deagle", "Deagle", Weapon_Pistol }, { "eagle", "Deagle", Weapon_Pistol },
        { "fx45", "FX-45", Weapon_Pistol }, { "fnx", "FX-45", Weapon_Pistol },
        { "m1914", "M1914", Weapon_Pistol }, { "m1911", "M1914", Weapon_Pistol }, { "colt", "M1914", Weapon_Pistol },
        { "m67sticky", "M67 Sticky", Weapon_Grenade }, { "sticky", "M67 Sticky", Weapon_Grenade },
        { "9bang", "Flash 9-Bang", Weapon_Grenade }, { "ninebang", "Flash 9-Bang", Weapon_Grenade },
        { "flash", "Flash", Weapon_Grenade }, { "rgn", "RGN Impact", Weapon_Grenade }, { "impact", "RGN Impact", Weapon_Grenade },
        { "mb78", "MB78", Weapon_Grenade }, { "smoke", "MB78", Weapon_Grenade },
        { "m67", "M67 Frag", Weapon_Grenade }, { "frag", "M67 Frag", Weapon_Grenade }, { "grenade", "M67 Frag", Weapon_Grenade },
        { "knife", "Knife", Weapon_Knife }, { "kamikaze", "Kamikaze", Weapon_Other }, { "sentinel", "Sentinel", Weapon_Other },
        { "bomb", "Bomb", Weapon_Other }, { "c4", "Bomb", Weapon_Other },
    };

    std::string Squash(const std::string& s) {
        std::string o;
        for (char c : s) if (std::isalnum(static_cast<unsigned char>(c))) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return o;
    }

    std::string TidyRow(std::string r) {
        for (const char* pre : { "wep_", "weapon_", "bp_", "da_", "dt_" })
            if (r.rfind(pre, 0) == 0) r = r.substr(std::strlen(pre));
        for (const char* suf : { "_default", "_base", "_c" }) {
            const size_t n = std::strlen(suf);
            if (r.size() > n && r.compare(r.size() - n, n, suf) == 0) r.resize(r.size() - n);
        }
        for (char& c : r) c = c == '_' ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return r;
    }

    bool FromRow(uintptr_t item, ItemInfo& info) {
        uint32_t idx = 0;
        if (!ReadRemote(item + kWepRowName, idx) || idx == 0) return false;
        info.rowIdx = idx;
        const std::string row = Skeleton_FName(idx);
        if (row.empty() || row == "none" || row == "None") return false;
        info.row = row;
        const std::string sq = Squash(row);
        for (const RowDef& d : kRows)
            if (sq.find(d.key) != std::string::npos) { info.name = d.name; if (!info.handItem) info.kind = d.kind; return true; }
        info.name = TidyRow(row);
        return true;
    }

    bool Plausible(uintptr_t p) { return p > 0x10000 && p < 0x7FFFFFFFFFFFull && (p & 7) == 0; }

    bool Classify(const std::vector<std::string>& chain, ItemInfo& out) {
        bool item = false;
        for (const std::string& n : chain) if (n == "bp_equippableitem_c") { item = true; break; }
        if (!item) return false;
        for (const std::string& n : chain) {
            if (n == "bp_perkproxy_c") { out.kind = Weapon_Other; out.name = "Drone"; out.handItem = true; return true; }
            for (const Def& d : kDefs) if (n == d.cls) { out.name = d.name; out.kind = d.kind; return true; }
            if (n == "grenade_c") { out.kind = Weapon_Grenade; out.name = "Grenade"; }
            else if (n == "knife_c") { out.kind = Weapon_Knife; out.name = "Knife"; }
            else if (n == "wep_c" && out.name.empty()) { out.kind = Weapon_Rifle; }
        }
        if (out.name.empty()) {
            std::string s = chain.front();
            if (s.size() > 2 && s.compare(s.size() - 2, 2, "_c") == 0) s.resize(s.size() - 2);
            for (char& c : s) c = c == '_' ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            out.name = s;
        }
        return true;
    }

    void ScanSlice() {
        const ULONGLONG now = GetTickCount64();
        if (now - g_lastSlice < 40) return;
        g_lastSlice = now;
        if (g_scanIdx >= g_scan.size()) {
            g_scan.clear();
            g_scanIdx = 0;
            const uintptr_t world = GetWorld();
            const uintptr_t level = world ? ReadPtr(world, BodycamOffsets::WorldPersistentLevel) : 0;
            uintptr_t data = 0; int32_t count = 0;
            if (level && ReadRemote(level + BodycamOffsets::LevelActors, data) &&
                ReadRemote(level + BodycamOffsets::LevelActors + 8, count) && Plausible(data) && count > 0 && count < 200000) {
                g_scan.resize(static_cast<size_t>(count));
                if (!ReadRemoteBytes(data, g_scan.data(), g_scan.size() * 8)) g_scan.clear();
            }
        }
        const size_t end = (std::min)(g_scan.size(), g_scanIdx + 1500);
        for (size_t i = g_scanIdx; i < end; ++i) {
            const uintptr_t a = g_scan[i];
            if (!Plausible(a) || g_items.count(a)) continue;
            const uintptr_t cls = ReadPtr(a, OFF(ObjectClass));
            if (!Plausible(cls)) continue;
            auto it = g_classIsItem.find(cls);
            ItemInfo info;
            if (it == g_classIsItem.end()) {
                const std::vector<std::string> chain = Skeleton_ClassChain(a);
                if (chain.empty()) continue;
                const bool isItem = Classify(chain, info);
                g_classIsItem[cls] = isItem;
                if (!isItem) continue;
            } else {
                if (!it->second) continue;
                Classify(Skeleton_ClassChain(a), info);
            }
            info.cls = cls;
            info.slot = i;
            g_items[a] = info;
        }
        g_scanIdx = end;
    }

    bool Alive(uintptr_t actor, uintptr_t cls) {
        if (ReadPtr(actor, OFF(ObjectClass)) != cls) return false;
        int32_t flags = 0;
        if (!ReadRemote(actor + OFF(ObjectFlags), flags)) return false;
        return (flags & (0x00008000 | 0x00010000 | 0x40000000)) == 0;
    }

    uint8_t TeamOfPawn(uintptr_t pawn) {
        const uintptr_t ps = pawn ? ReadPtr(pawn, kPawnPs) : 0;
        int32_t t = -1;
        if (!ps || !ReadRemote(ps + kPsTeam, t) || t < 0 || t > 64) return 0xFF;
        return static_cast<uint8_t>(t);
    }
}

namespace {

    bool ReadTransformAt(uintptr_t comp, ptrdiff_t off, FVector& t) {
        double q[4], tr[3], sc[3];
        if (!ReadRemote(comp + off, q) || !ReadRemote(comp + off + 0x20, tr) || !ReadRemote(comp + off + 0x40, sc)) return false;
        const double qn = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        if (!std::isfinite(qn) || std::fabs(qn - 1.0) > 0.01) return false;
        for (double v : sc) if (!(v > 0.01 && v < 100.0)) return false;
        for (double v : tr) if (!std::isfinite(v) || std::fabs(v) > 1.0e6) return false;
        t = { tr[0], tr[1], tr[2] };
        return t.LengthSq() > 1.0;
    }
    ptrdiff_t g_bodyC2W = 0x1D0;
    bool g_bodyC2WScanned = false;

    FVector GrenadeWorldPos(uintptr_t grenade) {
        const uintptr_t body = ReadPtr(grenade, OFF(ItemWepBody));
        FVector p{};
        if (body) {
            if (ReadTransformAt(body, g_bodyC2W, p)) return p;
            if (!g_bodyC2WScanned) {
                g_bodyC2WScanned = true;
                const FVector rel = GetActorLocation(grenade);
                ptrdiff_t best = -1; double bestD = 1e18;
                for (ptrdiff_t off = 0x140; off <= 0x2C0; off += 0x10) {
                    FVector t{};
                    if (!ReadTransformAt(body, off, t)) continue;
                    const double d = (t - rel).LengthSq();
                    if (d < bestD) { bestD = d; best = off; }
                }
                if (best >= 0) {
                    g_bodyC2W = best;
                    Log_Add(Log_Info, "World: grenade body ComponentToWorld at +0x%X", static_cast<unsigned>(best));
                    if (ReadTransformAt(body, best, p)) return p;
                }
            }
        }
        return GetActorLocation(grenade);
    }

    ItemInfo* Track(uintptr_t a) {
        if (!Plausible(a)) return nullptr;
        auto f = g_items.find(a);
        if (f != g_items.end()) return &f->second;
        const uintptr_t cls = ReadPtr(a, OFF(ObjectClass));
        if (!Plausible(cls)) return nullptr;
        auto it = g_classIsItem.find(cls);
        if (it != g_classIsItem.end() && !it->second) return nullptr;
        const std::vector<std::string> chain = Skeleton_ClassChain(a);
        if (chain.empty()) return nullptr;
        ItemInfo info;
        const bool isItem = Classify(chain, info);
        g_classIsItem[cls] = isItem;
        if (!isItem) return nullptr;
        info.cls = cls;
        info.fromRow = FromRow(a, info);
        return &(g_items[a] = std::move(info));
    }
}

void World_Update() {
    ScanSlice();
    const double now = Targets_Now();
    const TargetFrame& frame = Targets_Get();
    g_held.clear();
    std::vector<ThrownGrenade> grenades;
    std::unordered_set<uintptr_t> live;

    for (auto it = g_items.begin(); it != g_items.end();) {
        const uintptr_t a = it->first;
        ItemInfo& info = it->second;
        if (!Alive(a, info.cls)) { g_thrownSeen.erase(a); g_grenadeHolder.erase(a); it = g_items.erase(it); continue; }
        uintptr_t owner = 0;
        ReadRemote(a + kCharacterOwner, owner);
        if (info.kind == Weapon_Grenade && owner) g_grenadeHolder[a] = owner;
        if (!info.fromRow) info.fromRow = FromRow(a, info);
        else if (owner) {
            uint32_t idx = 0;
            if (ReadRemote(a + kWepRowName, idx) && idx && idx != info.rowIdx) FromRow(a, info);
        }
        bool visible = false;
        ReadRemote(a + kIsVisible, visible);

        if (info.kind == Weapon_Grenade) {
            bool thrown = false, blowing = false;
            ReadRemote(a + kIsThrown, thrown);
            ReadRemote(a + kIsBlowing, blowing);
            if (thrown && !blowing) {
                auto seen = g_thrownSeen.find(a);
                if (seen == g_thrownSeen.end()) seen = g_thrownSeen.emplace(a, now).first;
                ThrownGrenade g;
                g.actor = a;
                g.pos = GrenadeWorldPos(a);
                g.name = info.name;
                double fuse = 0.0;
                if (ReadRemote(a + kFuseDelay, fuse) && std::isfinite(fuse) && fuse > 0.05 && fuse < 30.0) g.fuse = fuse;
                g.thrownAt = seen->second;
                uintptr_t thrower = owner;
                if (!thrower) { const auto h = g_grenadeHolder.find(a); if (h != g_grenadeHolder.end()) thrower = h->second; }
                const uint8_t team = TeamOfPawn(thrower);
                g.mine = (thrower && thrower == frame.localPawn) || (a == g_localGrenade && g_localGrenade);
                g.friendly = g.mine || (team != 0xFF && team == frame.localTeam && Targets_TeamsTrusted());
                if (g.pos.LengthSq() > 1.0) { grenades.push_back(std::move(g)); live.insert(a); }
                ++it;
                continue;
            }
        }
        if (owner && visible) {
            HeldItem h;
            h.actor = a;
            h.kind = info.kind;
            h.name = info.name;
            if (info.kind != Weapon_Grenade && info.kind != Weapon_Knife && info.kind != Weapon_Other) {
                uintptr_t decoy = 0;
                h.reloading = ReadRemote(a + kReloadDecoy, decoy) && Plausible(decoy);
            }
            if (info.kind == Weapon_Grenade) {
                double s = 0.0;
                if (ReadRemote(a + kLaunchSpeed, s) && std::isfinite(s) && s > 100.0 && s < 20000.0) h.launchSpeed = s;
            }
            uintptr_t wep = 0;
            ReadRemote(owner + OFF(CharacterWep), wep);
            uintptr_t actorOwner = 0;
            ReadRemote(a + OFF(ActorOwner), actorOwner);
            const bool attached = actorOwner == owner;
            h.rank = (info.kind == Weapon_Grenade || info.kind == Weapon_Knife || info.handItem) ? 3
                   : a == wep ? 2 : (attached ? 1 : 0);
            auto cur = g_held.find(owner);
            if (cur == g_held.end() || h.rank >= cur->second.rank) g_held[owner] = std::move(h);
        }
        ++it;
    }
    if (frame.localPawn) {
        uintptr_t wep = 0;
        ReadRemote(frame.localPawn + OFF(CharacterWep), wep);
        auto cur = g_held.find(frame.localPawn);
        const bool handItem = cur != g_held.end() && cur->second.rank >= 3;
        if (wep && !handItem && (cur == g_held.end() || cur->second.actor != wep)) {
            if (ItemInfo* info = Track(wep); info && info->kind != Weapon_Grenade && Alive(wep, info->cls)) {
                HeldItem h;
                h.actor = wep; h.kind = info->kind; h.name = info->name; h.rank = 2;
                uintptr_t decoy = 0;
                h.reloading = ReadRemote(wep + kReloadDecoy, decoy) && Plausible(decoy);
                g_held[frame.localPawn] = std::move(h);
            }
        }
    }
    if (frame.localPawn) {
        const auto cur = g_held.find(frame.localPawn);
        if (cur != g_held.end() && cur->second.kind == Weapon_Grenade) g_localGrenade = cur->second.actor;
    }
    for (auto it = g_thrownSeen.begin(); it != g_thrownSeen.end();)
        it = live.count(it->first) ? std::next(it) : g_thrownSeen.erase(it);
    g_grenades = std::move(grenades);

    const uintptr_t ps = GetLocalPlayerState();
    int32_t k = -1, d = -1;
    if (ps && ReadRemote(ps + kPsKill, k) && ReadRemote(ps + kPsDeath, d) && k >= 0 && k < 100000 && d >= 0 && d < 100000) {
        if (g_deaths >= 0 && d > g_deaths) Log_Add(Log_Info, "Died (%d deaths)", d);
        g_kills = k; g_deaths = d;
    } else if (!ps) {
        g_kills = g_deaths = -1;
    }
}

const HeldItem* World_HeldBy(uintptr_t pawn) {
    const auto it = g_held.find(pawn);
    return it == g_held.end() ? nullptr : &it->second;
}
const std::vector<ThrownGrenade>& World_Grenades() { return g_grenades; }

bool World_ItemMeshPos(uintptr_t actor, FVector& out) {
    if (!actor) return false;
    const uintptr_t body = ReadPtr(actor, OFF(ItemWepBody));
    if (!body) return false;
    if (ReadTransformAt(body, g_bodyC2W, out)) return true;
    GrenadeWorldPos(actor);
    return ReadTransformAt(body, g_bodyC2W, out);
}
int World_LocalKills() { return g_kills; }
int World_LocalDeaths() { return g_deaths; }

std::string World_Status() {
    char b[128];
    std::snprintf(b, sizeof(b), "Items: %zu tracked, %zu held, %zu grenades", g_items.size(), g_held.size(), g_grenades.size());
    std::string out = b;
    std::string unknown;
    for (const auto& [a, info] : g_items) {
        if (!info.fromRow) continue;
        bool known = false;
        { const std::string sq = Squash(info.row); for (const RowDef& d : kRows) if (sq.find(d.key) != std::string::npos) known = true; }
        if (!known && unknown.find(info.row) == std::string::npos && unknown.size() < 60) unknown += (unknown.empty() ? "" : ", ") + info.row;
    }
    if (!unknown.empty()) out += "  \xc2\xb7  new: " + unknown;
    if (const uintptr_t lp = Targets_Get().localPawn) {
        if (const HeldItem* h = World_HeldBy(lp)) {
            const auto f = g_items.find(h->actor);
            out += "  \xc2\xb7  in hand: " + h->name + (f != g_items.end() && !f->second.row.empty() ? " (" + f->second.row + ")" : std::string());
        }
    }
    return out;
}
