#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <imgui.h>

enum TracerStyle { TracerStyle_Beam, TracerStyle_Line, TracerStyle_Laser, TracerStyle_Dashed, TracerStyle_Comet, TracerStyle_Rainbow, TracerStyle_Count };

struct MiscSettings {
    bool  NoRecoil     = false;
    bool  NoSpread     = false;
    bool  RapidFire    = false;
    float FireRateMul  = 2.0f;
    bool  InfiniteAmmo = false;
    bool  AutoFire     = false;
    float AutoFireCps  = 30.0f;
    bool  InstaReload  = false;
    float ReloadSpeedMul = 10.0f;
    bool  DamageBoost  = false;
    float DamageMul    = 2.0f;
    bool  WeaponSpeed  = false;
    float WeaponSpeedMul = 1.5f;
    bool  GodMode      = false;
    bool  NoDroneCooldown = false;
    bool  Wallbang        = false;
    bool  Fly          = false;
    bool  Noclip       = false;
    float FlySpeed     = 1200.0f;
    int   FlyUpKey     = 0x20;
    int   FlyDownKey   = 0x11;
    int   FlyBoostKey  = 0x10;
    float FlyResponse  = 6.0f;
    bool  Teleport     = false;
    int   TeleportKey  = 0x54;
    float TeleportDist = 150.0f;
    int   TeleportSide = 0;
    bool  AutoTeleport = false;
    bool  Speedhack    = false;
    float SpeedMul     = 1.5f;
    bool  FreezeBots   = false;
    float BotTimeScale = 0.0f;
    bool   Footsteps      = false;
    float  FootstepRange  = 40.0f;
    ImVec4 FootstepColor  = { 1.0f, 0.78f, 0.25f, 1.0f };
    bool   HitMarker      = false;
    float  HitMarkerSize  = 6.0f;
    ImVec4 HitColor       = { 1.0f, 1.0f, 1.0f, 1.0f };
    ImVec4 KillColor      = { 1.0f, 0.25f, 0.25f, 1.0f };
    bool   HitSound       = false;
    bool   Tracers        = false;
    float  TracerSeconds  = 1.5f;
    ImVec4 TracerColor    = { 0.47f, 0.40f, 1.0f, 1.0f };
    int    TracerStyle    = 0;
    float  TracerThickness = 1.0f;
    bool   TracerImpact   = true;
    bool   KillSound      = false;
    int    KillSoundType  = 0;
    float  SoundVolume    = 0.7f;
};
extern MiscSettings g_Misc;

void Misc_Update(bool gameForeground);
void Misc_Restore();
int Misc_IsHost();
float Misc_GodOrigMax();

void Fx_Update();
void Fx_Render(ImDrawList* dl, float w, float h);
void Fx_TestSound(bool kill);

void Host_Update();
void Host_Restore();
void Host_FreezeInput(bool on);
