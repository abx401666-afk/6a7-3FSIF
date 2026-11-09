#include "../include/CombatLog.hpp"
#include "../include/Targets.hpp"
#include "../include/World.hpp"
#include "../include/Ammo.hpp"
#include "../include/Aimbot.hpp"
#include "../include/ESP.hpp"
#include "../include/Log.hpp"
#include "../include/Memory.hpp"
#include "../include/Misc.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <unordered_map>

namespace {
    constexpr double kHitWindow  = 0.35;
    constexpr double kKillWindow = 0.8;

    struct Tracked {
        double health = -1.0, maxHealth = 100.0, seen = 0.0, lastHitByMe = -10.0;
        double dmgByMe = 0.0;
        int    hitsByMe = 0;
        float  distM = 0.0f, speed = 0.0f;
        FVector lastPos{};
        bool   bot = false, visible = false;
        std::string name;
    };
    struct Shot {
        double time = 0.0;
        uintptr_t aim = 0;
        bool aimVisible = false;
        float aimSpeed = 0.0f, aimDist = 0.0f;
        bool  projected = false;
        float offX = 0.0f, offY = 0.0f, bodyPx = 0.0f;
        float leadDir = 0.0f;
        bool  noSpread = false, aimbotLock = false;
        std::string weapon;
        int weaponKind = 0;
    };
    struct Death { uintptr_t pawn; double time; Tracked info; };

    std::unordered_map<uintptr_t, Tracked> g_tracked;
    std::deque<Shot> g_shots;
    std::deque<Death> g_deaths;
    std::vector<CombatEvent> g_events;
    uintptr_t g_heldActor = 0;
    int g_prevAmmo = -1, g_prevKills = -1, g_pendingKills = 0;
    double g_pendingKillAt = 0.0;
    int g_killPings = 0, g_pingCredit = 0;
    double g_pingCreditAt = -10.0;
    int g_shotCount = 0;

    const char* Dot() { return "  \xc2\xb7  "; }

    void Emit(int kind, const std::string& text, CombatEvent e = {}) {
        static const char* const kTag[] = { "KILL", "HIT", "MISS" };
        e.time = Targets_Now();
        e.kind = kind;
        e.tag = kTag[kind];
        e.text = text;
        if (g_events.size() >= 64) g_events.erase(g_events.begin());
        g_events.push_back(e);
        Log_Add(kind == Combat_Kill ? Log_Kill : kind == Combat_Hit ? Log_Hit : Log_Miss, "%s", text.c_str());
    }

    std::string NameOf(const Tracked& t) { return t.name.empty() ? (t.bot ? "Bot" : "Player") : t.name; }

    uintptr_t AimCandidate(const TargetFrame& f) {
        if (const uintptr_t lock = Aimbot_LockedTarget()) return lock;
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        uintptr_t best = 0;
        float bestD = 80.0f * 80.0f;
        for (const Target& t : f.targets) {
            if (t.friendly || t.drone) continue;
            ImVec2 s;
            if (!ESP_Project(t.pos, s)) continue;
            const float dx = s.x - ds.x * 0.5f, dy = s.y - ds.y * 0.5f;
            if (dx * dx + dy * dy < bestD) { bestD = dx * dx + dy * dy; best = t.pawn; }
        }
        return best;
    }

    std::string MissReason(const Shot& s) {
        char b[96];
        if (!s.aim) return "no enemy near crosshair";
        if (!s.aimVisible) return "behind cover (not visible)";
        if (s.projected) {
            const bool onBody = std::fabs(s.offX) <= 0.5f && std::fabs(s.offY) <= 0.5f;
            if (!onBody) {
                const bool horiz = std::fabs(s.offX) - 0.5f >= std::fabs(s.offY) - 0.5f;
                const float px = horiz ? (std::fabs(s.offX) - 0.5f) * s.bodyPx * 0.45f : (std::fabs(s.offY) - 0.5f) * s.bodyPx;
                if (horiz && s.aimSpeed > 150.0f && s.leadDir != 0.0f && (s.offX < 0.0f) == (s.leadDir > 0.0f)) {
                    std::snprintf(b, sizeof(b), "trailed a moving target (%.0f px behind)", px);
                    return b;
                }
                const char* dir = horiz ? (s.offX < 0.0f ? "left" : "right") : (s.offY < 0.0f ? "high" : "low");
                std::snprintf(b, sizeof(b), "aim %s of the body (%.0f px)", dir, px < 1.0f ? 1.0f : px);
                return b;
            }
            if (s.aimDist > 80.0f) { std::snprintf(b, sizeof(b), "on target, out of range (%.0f m)", s.aimDist); return b; }
            if (s.noSpread) return "on target, no damage registered";
            if (s.aimSpeed > 280.0f) return "on target, but it was running";
            return std::fabs(s.offX) > 0.3f || std::fabs(s.offY) > 0.35f ? "edge of the body (spread)" : "spread";
        }
        if (s.aimDist > 80.0f) { std::snprintf(b, sizeof(b), "out of range (%.0f m)", s.aimDist); return b; }
        return "no damage registered";
    }
}

void CombatLog_Update() {
    const double now = Targets_Now();
    const TargetFrame& f = Targets_Get();

    const HeldItem* held = f.localPawn ? World_HeldBy(f.localPawn) : nullptr;
    const uintptr_t heldActor = held ? held->actor : 0;
    const int ammo = Ammo_Current();
    if (heldActor != g_heldActor) { g_heldActor = heldActor; g_prevAmmo = ammo; }
    else if (ammo >= 0 && g_prevAmmo >= 0 && ammo < g_prevAmmo && g_prevAmmo - ammo <= 3 &&
             held && held->kind != Weapon_Grenade && held->kind != Weapon_Knife) {
        const uintptr_t aim = AimCandidate(f);
        const Target* at = nullptr;
        for (const Target& t : f.targets) if (t.pawn == aim) at = &t;
        for (int i = 0; i < g_prevAmmo - ammo; ++i) {
            Shot s;
            s.time = now;
            s.aim = aim;
            s.weapon = held->name;
            s.weaponKind = held->kind;
            if (at) {
                s.aimVisible = at->visible; s.aimSpeed = static_cast<float>(std::hypot(at->vel.X, at->vel.Y)); s.aimDist = at->distM;
                ImVec2 top, bot, ahead;
                const FVector head{ at->pos.X, at->pos.Y, at->pos.Z + at->halfHeight }, feet{ at->pos.X, at->pos.Y, at->pos.Z - at->halfHeight };
                if (ESP_Project(head, top) && ESP_Project(feet, bot) && bot.y - top.y > 2.0f) {
                    const ImVec2 ds = ImGui::GetIO().DisplaySize;
                    const float hgt = bot.y - top.y, wid = hgt * 0.45f;
                    const float cx = (top.x + bot.x) * 0.5f, cy = (top.y + bot.y) * 0.5f;
                    s.projected = true;
                    s.bodyPx = hgt;
                    s.offX = (ds.x * 0.5f - cx) / wid;
                    s.offY = (ds.y * 0.5f - cy) / hgt;
                    const FVector fut = at->pos + at->vel * 0.1;
                    if (ESP_Project(fut, ahead)) s.leadDir = ahead.x > cx + 0.5f ? 1.0f : ahead.x < cx - 0.5f ? -1.0f : 0.0f;
                }
            }
            s.noSpread = g_Misc.NoSpread;
            g_shots.push_back(s);
            ++g_shotCount;
        }
        g_prevAmmo = ammo;
    } else if (ammo >= 0) {
        g_prevAmmo = ammo;
    }

    std::unordered_map<uintptr_t, bool> present;
    for (const Target& t : f.targets) {
        if (t.drone || t.friendly) continue;
        present[t.pawn] = true;
        Tracked& tr = g_tracked[t.pawn];
        if (tr.seen > 0.0 && tr.health > 0.0 && t.health >= 0.0 && t.health < tr.health - 0.5 && !t.stale) {
            auto best = g_shots.end();
            for (auto it = g_shots.begin(); it != g_shots.end(); ++it) {
                if (now - it->time > kHitWindow) continue;
                if (best == g_shots.end() || (it->aim == t.pawn && best->aim != t.pawn)) best = it;
            }
            if (best != g_shots.end()) {
                const double dmg = tr.health - t.health;
                tr.dmgByMe += dmg;
                ++tr.hitsByMe;
                tr.lastHitByMe = now;
                char b[160];
                std::snprintf(b, sizeof(b), "%s%s%.0f dmg%s%.0f hp left%s%.0f m", NameOf(tr).c_str(), Dot(), dmg, Dot(),
                              (std::max)(0.0, t.health), Dot(), t.distM);
                if (tr.name.empty()) tr.name = t.name;
                CombatEvent e;
                e.who = NameOf(tr); e.weapon = best->weapon; e.weaponKind = best->weaponKind;
                e.dmg = static_cast<float>(dmg); e.hpLeft = static_cast<float>((std::max)(0.0, t.health));
                e.maxHp = static_cast<float>(t.maxHealth > 1.0 ? t.maxHealth : 100.0); e.distM = t.distM; e.hits = tr.hitsByMe;
                e.pawn = t.pawn; e.px = t.pos.X; e.py = t.pos.Y; e.pz = t.pos.Z + t.halfHeight * 0.45;
                if (t.health <= 0.5) { ++g_killPings; ++g_pingCredit; g_pingCreditAt = now; }
                Emit(Combat_Hit, b, e);
                g_shots.erase(best);
            }
        }
        if (!t.stale) {
            tr.health = t.health; tr.maxHealth = t.maxHealth;
            tr.distM = t.distM; tr.speed = static_cast<float>(std::hypot(t.vel.X, t.vel.Y));
            tr.visible = t.visible; tr.bot = t.bot; tr.name = t.name;
            tr.lastPos = { t.pos.X, t.pos.Y, t.pos.Z + t.halfHeight * 0.45 };
        }
        tr.seen = now;
    }

    for (auto it = g_tracked.begin(); it != g_tracked.end();) {
        if (present.count(it->first)) { ++it; continue; }
        const uintptr_t pawn = it->first;
        const double hp = GetPlayerHealth(pawn);
        if (now - it->second.seen < 1.0 && (IsPlayerDead(pawn) || (hp >= 0.0 && hp <= 0.5) || hp < 0.0))
            g_deaths.push_back({ pawn, now, it->second });
        it = g_tracked.erase(it);
    }
    while (!g_deaths.empty() && now - g_deaths.front().time > kKillWindow) g_deaths.pop_front();

    const int kills = World_LocalKills();
    if (kills < 0) { g_prevKills = -1; g_pendingKills = 0; }
    else {
        if (g_prevKills >= 0 && kills > g_prevKills && kills - g_prevKills <= 5) {
            g_pendingKills += kills - g_prevKills; g_pendingKillAt = now;
            if (now - g_pingCreditAt > 1.5) g_pingCredit = 0;
            for (int i = kills - g_prevKills; i > 0; --i) { if (g_pingCredit > 0) --g_pingCredit; else ++g_killPings; }
        }
        if (g_prevKills >= 0 && kills < g_prevKills) { g_shotCount = 0; g_events.clear(); }
        g_prevKills = kills;
    }
    if (g_pendingKills > 8) g_pendingKills = 8;
    while (g_pendingKills > 0) {
        auto best = g_deaths.end();
        for (auto it = g_deaths.begin(); it != g_deaths.end(); ++it)
            if (best == g_deaths.end() || it->info.lastHitByMe > best->info.lastHitByMe) best = it;
        if (best == g_deaths.end()) {
            if (now - g_pendingKillAt < kKillWindow) break;
            char b[96];
            std::snprintf(b, sizeof(b), "Enemy%s%s", Dot(), held && !held->name.empty() ? held->name.c_str() : "kill");
            CombatEvent e;
            e.who = "Enemy";
            if (held) { e.weapon = held->name; e.weaponKind = held->kind; }
            Emit(Combat_Kill, b, e);
        } else {
            const Tracked& v = best->info;
            char b[192];
            std::snprintf(b, sizeof(b), "%s%s%s%s%.0f m%s%d hit%s, %.0f dmg", NameOf(v).c_str(), Dot(),
                          held && !held->name.empty() ? held->name.c_str() : "?", Dot(), v.distM, Dot(),
                          v.hitsByMe, v.hitsByMe == 1 ? "" : "s", v.dmgByMe);
            CombatEvent e;
            e.who = NameOf(v);
            if (held) { e.weapon = held->name; e.weaponKind = held->kind; }
            e.distM = v.distM; e.hits = v.hitsByMe; e.dmg = static_cast<float>(v.dmgByMe);
            e.pawn = best->pawn; e.px = v.lastPos.X; e.py = v.lastPos.Y; e.pz = v.lastPos.Z;
            Emit(Combat_Kill, b, e);
            g_deaths.erase(best);
        }
        --g_pendingKills;
    }

    while (!g_shots.empty() && now - g_shots.front().time > kHitWindow) {
        const Shot s = g_shots.front();
        g_shots.pop_front();
        if (!s.aim) continue;
        std::string who;
        { const auto it = g_tracked.find(s.aim); if (it != g_tracked.end()) who = NameOf(it->second); }
        if (who.empty()) continue;
        char b[160];
        std::snprintf(b, sizeof(b), "%s%s%s%s%s", who.c_str(), Dot(), MissReason(s).c_str(), Dot(), s.weapon.c_str());
        CombatEvent e;
        e.who = who; e.weapon = s.weapon; e.weaponKind = s.weaponKind; e.reason = MissReason(s); e.distM = s.aimDist;
        Emit(Combat_Miss, b, e);
    }
}

const std::vector<CombatEvent>& CombatLog_Events() { return g_events; }
int CombatLog_Shots() { return g_shotCount; }

int CombatLog_KillPings() { return g_killPings; }
