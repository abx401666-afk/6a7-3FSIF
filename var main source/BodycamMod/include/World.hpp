#pragma once
#include "UETypes.hpp"
#include <cstdint>
#include <string>
#include <vector>

enum WeaponKind_ : int {
    Weapon_None = 0, Weapon_Rifle, Weapon_SMG, Weapon_Pistol, Weapon_Shotgun, Weapon_Sniper,
    Weapon_Grenade, Weapon_Knife, Weapon_Other, Weapon_Count
};

struct HeldItem {
    uintptr_t   actor = 0;
    int         kind = Weapon_None;
    std::string name;
    bool        reloading = false;
    int         rank = 0;
    double      launchSpeed = 0.0;
};

struct ThrownGrenade {
    uintptr_t   actor = 0;
    FVector     pos{};
    std::string name;
    double      fuse = 0.0;
    double      thrownAt = 0.0;
    bool        friendly = false;
    bool        mine = false;
};

void World_Update();
const HeldItem* World_HeldBy(uintptr_t pawn);
const std::vector<ThrownGrenade>& World_Grenades();
bool World_ItemMeshPos(uintptr_t actor, FVector& out);
int  World_LocalKills();
int  World_LocalDeaths();
std::string World_Status();
