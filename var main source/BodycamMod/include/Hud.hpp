#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>

enum HudWidget_ : int { Hud_Watermark = 0, Hud_Ammo, Hud_Keybinds, Hud_Stats, Hud_Target, Hud_Kills, Hud_Health, Hud_CombatLog, Hud_Music, Hud_Count };
enum TargetHudStyle_ : int { TargetHud_Line = 0, TargetHud_Circle };

struct HudConfig {
    bool  Watermark     = true;
    bool  WmBrand       = true;
    bool  WmUser        = true;
    bool  WmStatus      = true;
    bool  WmFps         = true;
    bool  WmTime        = false;

    bool  Ammo          = true;
    bool  AmmoBar       = true;
    float AmmoLowPct    = 25.0f;
    bool  AmmoWeaponIcon = true;

    bool  Kills         = true;
    bool  KillsDeaths   = true;
    bool  KillsKD       = true;

    bool  Health        = true;
    bool  HealthNumber  = true;
    bool  HealthDamageTrail = true;

    bool  Keybinds      = true;
    bool  KbOnlyActive  = false;

    bool  Stats         = true;
    bool  StatsVisible  = true;

    bool  TargetHud     = true;
    int   TargetStyle   = TargetHud_Circle;
    bool  TargetName    = true;
    bool  TargetDist    = true;
    bool  TargetBar     = true;

    bool  Notifications = true;
    float NotifySeconds = 2.5f;
    bool  NotifyCustomColor = false;
    ImVec4 NotifyColor  = { 0.482f, 0.424f, 0.965f, 1 };

    bool  CombatLog     = true;
    bool  ClKills       = true;
    bool  ClHits        = true;
    bool  ClMisses      = false;
    float ClSeconds     = 5.0f;
    int   ClMaxLines    = 6;
    ImVec4 ClKillColor  = { 0.35f, 0.90f, 0.50f, 1 };
    ImVec4 ClHitColor   = { 0.482f, 0.424f, 0.965f, 1 };
    ImVec4 ClMissColor  = { 1.00f, 0.38f, 0.38f, 1 };

    bool  Music         = false;
    bool  MusicButtons  = true;
    bool  MusicBars     = true;

    float PosX[Hud_Count] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    float PosY[Hud_Count] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };

    bool   CustomColor[Hud_Count] = {};
    ImVec4 Color[Hud_Count] = {
        { 0.482f, 0.424f, 0.965f, 1 }, { 0.482f, 0.424f, 0.965f, 1 }, { 0.482f, 0.424f, 0.965f, 1 }, { 0.482f, 0.424f, 0.965f, 1 },
        { 0.482f, 0.424f, 0.965f, 1 }, { 0.482f, 0.424f, 0.965f, 1 }, { 0.482f, 0.424f, 0.965f, 1 },
        { 0.482f, 0.424f, 0.965f, 1 }, { 0.482f, 0.424f, 0.965f, 1 } };
};

extern HudConfig g_Hud;

void Hud_Render(bool menuOpen);
void Hud_Notify(const char* icon, const char* text);
void Hud_NotifyAlways(const char* icon, const char* text, float seconds);
void Hud_RenderToasts();
void Hud_ResetPositions();
