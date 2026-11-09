#pragma once
#include <string>
#include <vector>

enum CombatKind_ : int { Combat_Kill = 0, Combat_Hit, Combat_Miss };

struct CombatEvent {
    double      time = 0.0;
    int         kind = Combat_Hit;
    std::string text;
    std::string tag;
    std::string who;
    std::string weapon;
    int         weaponKind = 0;
    float       dmg = 0.0f, hpLeft = -1.0f, maxHp = 100.0f, distM = 0.0f;
    int         hits = 0;
    std::string reason;
    uintptr_t   pawn = 0;
    double      px = 0, py = 0, pz = 0;
};

void CombatLog_Update();
const std::vector<CombatEvent>& CombatLog_Events();
int  CombatLog_KillPings();
int  CombatLog_Shots();
