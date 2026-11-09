#pragma once

#include "UETypes.hpp"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <initializer_list>
#include <vector>

bool AttachToGameProcess();
void DetachFromGameProcess();
bool IsGameProcessAttached();
bool IsGameProcessAlive();
DWORD GetAttachedProcessId();
uintptr_t GetRemoteModuleBase();
HANDLE GetRemoteProcessHandle();
bool ReadRemoteBytes(uintptr_t address, void* destination, size_t size);

template <typename T>
inline bool ReadRemote(uintptr_t address, T& value) {
    static_assert(std::is_trivially_copyable_v<T>, "ReadRemote requires a trivially copyable type");
    return ReadRemoteBytes(address, &value, sizeof(T));
}

template <typename T>
inline T ReadRemoteOr(uintptr_t address, T fallback = T{}) {
    T value{};
    return ReadRemote(address, value) ? value : fallback;
}

uintptr_t ReadPtr(uintptr_t base, ptrdiff_t offset);

uintptr_t GetWorld();
bool IsObjectAlive(uintptr_t object);
uintptr_t GetLocalPlayerController();
uintptr_t GetLocalPawn();
uintptr_t GetLocalPlayerState();
uintptr_t GetCameraManager();
uintptr_t GetGameState();
uintptr_t GetSurvivorStatus(uintptr_t player);
uintptr_t GetPawnFromPlayerState(uintptr_t playerState);

struct FCameraCache {
    uint8_t Pad[0x10]{};
    FVector Location{};
    FRotator Rotation{};
    float FOV = 0.0f;
};
static_assert(offsetof(FCameraCache, Location) == 0x10);
static_assert(offsetof(FCameraCache, Rotation) == 0x28);
static_assert(offsetof(FCameraCache, FOV) == 0x40);

FCameraCache GetCameraCache();
bool GetControlRotation(FRotator& rotation);
bool IsLocalPawnAiming();
std::vector<uintptr_t> GetPlayerArray();
std::vector<uintptr_t> GetDronePawns();
float GetWorldTimeSeconds();
FVector GetActorLocation(uintptr_t actor);
bool GetCapsuleDimensions(uintptr_t actor, float& radius, float& halfHeight);
FRotator GetActorRotation(uintptr_t actor);
uintptr_t GetMeshComponent(uintptr_t pawn);
bool IsLikelyDronePawn(uintptr_t pawn);
std::string GetPlayerName(uintptr_t playerState);
double GetPlayerHealth(uintptr_t player);
double GetPlayerMaxHealth(uintptr_t player);
bool   GetPlayerHealthLive(uintptr_t player, double& health, double& maxHealth);
bool IsPlayerDead(uintptr_t player);
struct PawnEntry {
    uintptr_t pawn = 0;
    uintptr_t playerState = 0;
};
std::vector<PawnEntry> GetCharacterPawns();

uint8_t GetPawnTeam(uintptr_t pawn);
uint8_t GetLocalTeam();
bool IsBotPawn(uintptr_t pawn);

constexpr double kPi = 3.14159265358979323846;
inline double DegToRad(double degrees) { return degrees * (kPi / 180.0); }
inline double RadToDeg(double radians) { return radians * (180.0 / kPi); }

inline FRotator CalcAngle(const FVector& from, const FVector& to) {
    const FVector delta = to - from;
    const double distance2D = sqrt(delta.X * delta.X + delta.Y * delta.Y);
    FRotator result{};
    result.Pitch = RadToDeg(atan2(delta.Z, distance2D));
    result.Yaw = RadToDeg(atan2(delta.Y, delta.X));
    result.Roll = 0.0;
    return result;
}

inline bool WorldToScreen(const FVector& worldPos, const FVector& cameraPos,
                          const FRotator& cameraRot, float fov,
                          float screenW, float screenH, float& outX, float& outY) {
    const double pitch = DegToRad(cameraRot.Pitch);
    const double yaw = DegToRad(cameraRot.Yaw);
    const double roll = DegToRad(cameraRot.Roll);
    const FVector forward{ cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch) };
    const FVector unrolledRight{ -sin(yaw), cos(yaw), 0.0 };
    const FVector unrolledUp{ -sin(pitch) * cos(yaw), -sin(pitch) * sin(yaw), cos(pitch) };
    const FVector right = unrolledRight * cos(roll) - unrolledUp * sin(roll);
    const FVector up = unrolledUp * cos(roll) + unrolledRight * sin(roll);
    const FVector delta = worldPos - cameraPos;
    const double ax = delta.Dot(right);
    const double ay = delta.Dot(up);
    const double az = delta.Dot(forward);
    if (az <= 0.01 || fov <= 0.01f) return false;
    const double tangent = tan(DegToRad(static_cast<double>(fov) * 0.5));
    if (!std::isfinite(tangent) || std::abs(tangent) < 1e-9) return false;
    const double pixelScale = screenW * 0.5;
    outX = static_cast<float>(screenW * 0.5 + (ax / az / tangent) * pixelScale);
    outY = static_cast<float>(screenH * 0.5 - (ay / az / tangent) * pixelScale);
    return std::isfinite(outX) && std::isfinite(outY);
}
