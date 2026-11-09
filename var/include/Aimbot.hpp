#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <Windows.h>
#include <imgui.h>

enum AimBone_ : int { AimBone_Head = 0, AimBone_Neck, AimBone_Chest, AimBone_Pelvis, AimBone_Legs, AimBone_Count };
enum AimPriority_ : int { AimPriority_Crosshair = 0, AimPriority_Distance, AimPriority_Health, AimPriority_Count };
enum AimKeyMode_ : int { AimKeyMode_Hold = 0, AimKeyMode_Toggle, AimKeyMode_Always, AimKeyMode_Count };

struct AimbotConfig {
    bool     Enabled        = true;
    int      KeyMode        = AimKeyMode_Hold;
    int      AimKey         = VK_RBUTTON;
    unsigned TargetBones    = 1u << AimBone_Chest;
    int      Priority       = AimPriority_Crosshair;

    float    FOV            = 40.0f;
    float    Smoothing      = 8.0f;
    float    MaxDistance    = 300.0f;
    bool     OnlyVisible    = false;
    bool     TargetDrones   = false;
    bool     StickyTarget   = true;
    float    SwitchDelayMs  = 150.0f;

    bool     Prediction     = false;
    float    PredictionMs   = 60.0f;


    bool     Triggerbot     = false;
    int      TriggerKey     = 0;
    float    TriggerDelayMs = 40.0f;
    bool     TriggerVisibleOnly = true;

    bool     DrawFOV        = true;
    bool     DrawTarget     = true;
    ImVec4   FovColor       = { 1.0f, 1.0f, 1.0f, 0.4f };
    ImVec4   MarkerColor    = { 1.0f, 0.24f, 0.24f, 0.9f };
    int      MarkerStyle    = 0;
    float    MarkerSize     = 6.0f;
    bool     MarkerOutline  = true;
    bool     MarkerSpin     = false;
};

void Aimbot_Tick(const AimbotConfig& cfg);
void Aimbot_Render(const AimbotConfig& cfg, float screenW, float screenH);
void Aimbot_Suspend();
uintptr_t Aimbot_LockedTarget();
bool Aimbot_Toggled();
