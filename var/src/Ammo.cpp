#include "../include/Ammo.hpp"
#include "../include/Memory.hpp"
#include "../include/Skeleton.hpp"
#include "../include/Log.hpp"
#include <cmath>
#include <cstdio>
#include <string>

namespace {
    const int64_t& kWeaponSet     = OFF(WeaponSet);
    const int64_t& kMagAmount     = OFF(WaMagAmount);
    const int64_t& kAmmoCapacity  = OFF(WaAmmoCapacity);
    const int64_t& kCurrentAmmo   = OFF(WaCurrentAmmo);
    const int64_t& kCurrentValue  = OFF(AttrCurrentValue);

    uintptr_t g_set = 0, g_checkedSet = 0;
    bool g_setOk = false;
    int g_value = -1, g_capacity = 0, g_mags = -1;

    bool Plausible(uintptr_t p) { return p > 0x10000 && p < 0x7FFFFFFFFFFFull && (p & 7) == 0; }

    bool ReadAttr(uintptr_t set, ptrdiff_t field, int& out) {
        float v = 0.0f;
        if (!ReadRemote(set + field + kCurrentValue, v) || !std::isfinite(v) || v < 0.0f || v > 10000.0f) return false;
        out = static_cast<int>(std::lround(v));
        return true;
    }
}

void Ammo_Update(bool) {
    g_value = -1;
    const uintptr_t pawn = GetLocalPawn();
    uintptr_t set = 0;
    if (!pawn || !ReadRemote(pawn + kWeaponSet, set) || !Plausible(set)) { g_set = 0; return; }
    if (set != g_checkedSet) {
        g_checkedSet = set;
        g_setOk = false;
        for (const std::string& n : Skeleton_ClassChain(set))
            if (n.find("weaponattributeset") != std::string::npos) { g_setOk = true; break; }
        if (g_setOk) Log_Add(Log_Info, "Ammo: weapon attribute set found");
        else Log_Add(Log_Warn, "Ammo: pawn+0x698 is not a WeaponAttributeSet");
    }
    g_set = g_setOk ? set : 0;
    if (!g_set) return;
    int cur = -1, cap = 0, mags = -1;
    if (!ReadAttr(g_set, kCurrentAmmo, cur)) return;
    if (!ReadAttr(g_set, kAmmoCapacity, cap)) cap = 0;
    if (!ReadAttr(g_set, kMagAmount, mags)) mags = -1;
    g_value = cur;
    g_capacity = cap > 0 ? cap : (cur > g_capacity ? cur : g_capacity);
    g_mags = mags;
}

int  Ammo_Current() { return g_value; }
int  Ammo_MaxSeen() { return g_capacity; }
bool Ammo_Learned() { return g_set != 0; }

std::string Ammo_Status() {
    char b[128];
    if (!g_set) std::snprintf(b, sizeof(b), "Ammo: weapon attributes not found");
    else std::snprintf(b, sizeof(b), "Ammo: %d / %d, mags %d (WeaponSet +0x1E0)", g_value, g_capacity, g_mags);
    return b;
}
