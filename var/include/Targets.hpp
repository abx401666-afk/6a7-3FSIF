#pragma once
#include "Memory.hpp"
#include <string>
#include <vector>

struct Target {
    uintptr_t pawn = 0;
    uintptr_t state = 0;
    bool      drone = false;
    bool      bot = false;
    bool      friendly = false;
    bool      visible = false;
    bool      stale = false;
    uint8_t   team = 0xFF;
    FVector   pos{};
    FVector   vel{};
    float     radius = 42.0f;
    float     halfHeight = 92.0f;
    double    yaw = 0.0;
    double    health = -1.0;
    double    maxHealth = 100.0;
    float     distM = 0.0f;
    std::string name;
};

struct TargetFrame {
    double        time = 0.0;
    FCameraCache  cam{};
    bool          camValid = false;
    uintptr_t     localPawn = 0;
    uint8_t       localTeam = 0xFF;
    std::vector<Target> targets;
};


void Targets_Update();
const TargetFrame& Targets_Get();
bool Targets_TeamsTrusted();

double Targets_Now();
bool   Targets_CameraUsable(const FCameraCache& cam);

FVector      Targets_PositionAt(uintptr_t pawn, double t, const FVector& fallback);
FCameraCache Targets_CameraAt(double t, const FCameraCache& fallback);
double Targets_GameFrameTime();
