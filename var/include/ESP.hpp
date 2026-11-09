#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "UETypes.hpp"
#include <imgui.h>

enum BoxStyle_ : int { BoxStyle_Full = 0, BoxStyle_Corner, BoxStyle_3D, BoxStyle_3DCorner, BoxStyle_Count };
enum SnapOrigin_ : int { SnapOrigin_Bottom = 0, SnapOrigin_Center, SnapOrigin_Top, SnapOrigin_Count };
enum HealthPos_ : int { HealthPos_Left = 0, HealthPos_Right, HealthPos_Top, HealthPos_Bottom, HealthPos_Count };
enum HealthColor_ : int { HealthColor_Gradient = 0, HealthColor_Fade, HealthColor_Solid, HealthColor_Count };
enum WeaponMode_ : int { WeaponMode_Text = 0, WeaponMode_Icon, WeaponMode_Both, WeaponMode_Count };

struct ESPConfig {
    bool  Enabled          = true;

    bool  ShowBoxes        = true;
    int   BoxStyle         = BoxStyle_Corner;
    bool  FilledBoxes      = false;
    float BoxThickness     = 1.5f;

    bool  ShowBones        = false;
    float BoneThickness    = 1.2f;
    bool  ShowHeadCircle   = false;
    bool  ShowViewDir      = false;
    float ViewDirLength    = 120.0f;
    bool  ViewDirCustomColor = false;
    ImVec4 ViewDirColor    = {1.0f, 1.0f, 1.0f, 0.9f};

    bool  ShowNames        = false;
    bool  ShowTypeTag      = false;
    bool  ShowHealth       = false;
    bool  ShowHealthText   = false;
    int   HealthPos        = HealthPos_Left;
    float HealthWidth      = 3.0f;
    int   HealthColorMode  = HealthColor_Gradient;
    bool  HealthOutline    = true;
    bool  HealthBackground = true;
    ImVec4 HealthFullColor = {0.25f, 1.0f, 0.35f, 1.0f};
    ImVec4 HealthLowColor  = {1.0f, 0.16f, 0.16f, 1.0f};
    bool  NameCustomColor  = false;
    ImVec4 NameColor       = {1.0f, 1.0f, 1.0f, 1.0f};
    bool  ShowDistance     = true;
    ImVec4 DistanceColor   = {0.86f, 0.86f, 0.86f, 0.9f};
    bool  ShowWeapon       = false;
    int   WeaponMode       = WeaponMode_Text;
    ImVec4 WeaponColor     = {0.95f, 0.95f, 0.95f, 0.95f};
    bool  WeaponReloading  = true;

    bool  ShowSnaplines    = false;
    int   SnaplineOrigin   = SnapOrigin_Bottom;
    bool  SnapCustomColor  = false;
    ImVec4 SnapColor       = {1.0f, 1.0f, 1.0f, 0.6f};
    float SnapThickness    = 1.0f;
    bool  ShowOffscreen    = true;
    bool  OffscreenCustomColor = false;
    ImVec4 OffscreenColor  = {1.0f, 0.3f, 0.3f, 1.0f};
    float OffscreenRadius  = 160.0f;
    float OffscreenSize    = 11.0f;
    bool  ShowEnemyCount   = false;

    bool  VisibilityColors = true;
    bool  ShowTeammates    = true;
    bool  ShowDrones       = true;
    bool  ShowFriendlyDrones = true;
    bool  DroneLabel       = true;

    bool  ShowGrenades     = true;
    bool  GrenadeTimer     = true;
    bool  GrenadeRing      = true;
    float GrenadeRadius    = 6.0f;
    ImVec4 GrenadeColor    = {1.0f, 0.55f, 0.1f, 1.0f};
    ImVec4 GrenadeFriendlyColor = {0.25f, 0.95f, 0.85f, 1.0f};
    float MaxDistance      = 500.0f;
    bool  DistanceFade     = false;


    ImVec4 BoxColor        = {1.0f, 0.2f, 0.2f, 1.0f};
    ImVec4 HiddenColor     = {1.0f, 0.2f, 0.2f, 1.0f};
    ImVec4 VisibleColor    = {0.3f, 1.0f, 0.3f, 1.0f};
    ImVec4 DroneColor      = {0.2f, 0.7f, 1.0f, 1.0f};
    ImVec4 FriendlyDroneColor = {0.25f, 0.95f, 0.55f, 1.0f};
    ImVec4 TeammateColor   = {0.25f, 0.95f, 0.85f, 1.0f};
    ImVec4 BoneColor       = {1.0f, 1.0f, 0.0f, 0.85f};
};

void ESP_Render(const ESPConfig& cfg, ImDrawList* dl, float screenW, float screenH);

bool ESP_Project(const FVector& world, ImVec2& out);
bool ESP_ProjectSegment(const FVector& a, const FVector& b, ImVec2& outA, ImVec2& outB);

float ESP_FovAdjust();
