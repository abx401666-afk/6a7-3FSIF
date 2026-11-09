#pragma once
#include "GameOffsets.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <string>

inline std::string WideToUtf8(const wchar_t* ws, int len) {
    if (!ws || len <= 0) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, len, out.data(), n, nullptr, nullptr);
    return out;
}

namespace Offsets {
    inline const int64_t& GObjects = OFF(GObjects);
    inline const int64_t& GNames = OFF(GNames);
    inline const int64_t& GWorld = OFF(GWorld);
    inline const int64_t& AppendString = OFF(AppendString);
    inline const int64_t& ProcessEvent = OFF(ProcessEvent);
}

namespace BodycamOffsets {
    inline const int64_t& WorldPersistentLevel = OFF(WorldPersistentLevel);
    inline const int64_t& LevelActors = OFF(LevelActors);
    inline const int64_t& ObjectOuter = OFF(ObjectOuter);
    inline const int64_t& SkeletalRecentlyRenderedByte = OFF(SkeletalRecentlyRenderedByte);
    inline const int64_t& WorldGameState = OFF(WorldGameState);
    inline const int64_t& WorldOwningGameInstance = OFF(WorldOwningGameInstance);
    inline const int64_t& GameInstanceLocalPlayers = OFF(GameInstanceLocalPlayers);
    inline const int64_t& LocalPlayerController = OFF(LocalPlayerController);
    inline const int64_t& ControllerPlayerState = OFF(ControllerPlayerState);
    inline const int64_t& ControllerAcknowledgedPawn = OFF(ControllerAcknowledgedPawn);
    inline const int64_t& ControllerCameraManager = OFF(ControllerCameraManager);
    inline const int64_t& ControllerControlRotation = OFF(ControllerControlRotation);
    inline const int64_t& BodycamPlayerAiming = OFF(BodycamPlayerAiming);
    inline const int64_t& PlayerArray = OFF(PlayerArray);
    inline const int64_t& PlayerStatePawn = OFF(PlayerStatePawn);
    inline const int64_t& PlayerStateName = OFF(PlayerStateName);
    inline const int64_t& BodycamPlayerTeam = OFF(BodycamPlayerTeam);
    inline const int64_t& BodycamPlayerControllerTeam = OFF(BodycamPlayerControllerTeam);
    inline const int64_t& AIHumanoidTeam = OFF(AIHumanoidTeam);
    inline const int64_t& BodycamPlayerCamera = OFF(BodycamPlayerCamera);
    inline const int64_t& CharacterMesh = OFF(CharacterMesh);
    inline const int64_t& CharacterCapsule = OFF(CharacterCapsule);
    inline const int64_t& CapsuleHalfHeight = OFF(CapsuleHalfHeight);
    inline const int64_t& CapsuleRadius = OFF(CapsuleRadius);
    inline const int64_t& ActorRootComponent = OFF(ActorRootComponent);
    inline const int64_t& SceneRelativeLocation = OFF(SceneRelativeLocation);
    inline const int64_t& SceneRelativeRotation = OFF(SceneRelativeRotation);
    inline const int64_t& SceneRelativeScale = OFF(SceneRelativeScale);
    inline const int64_t& SkeletalComponentTransforms = OFF(SkeletalComponentTransforms);
    inline const int64_t& CameraCache = OFF(CameraCache);
    inline const int64_t& DroneCamera = OFF(DroneCamera);
    inline const int64_t& DroneMovement = OFF(DroneMovement);
    inline const int64_t& DroneHitSphere = OFF(DroneHitSphere);
    inline const int64_t& PlayerSurvivorStatus = OFF(PlayerSurvivorStatus);
    inline const int64_t& ZombieSurvivorStatus = OFF(ZombieSurvivorStatus);
    inline const int64_t& SurvivorStatusDeath = OFF(SurvivorStatusDeath);
    inline const int64_t& SurvivorStatusHealth = OFF(SurvivorStatusHealth);
    inline const int64_t& SurvivorStatusMaxHealth = OFF(SurvivorStatusMaxHealth);
    inline const int64_t& ActorReplicatedLocation = OFF(ActorReplicatedLocation);
    inline const int64_t& ActorReplicatedRotation = OFF(ActorReplicatedRotation);
}

struct FString {
    wchar_t* Data  = nullptr;
    int32_t  Count = 0;
    int32_t  Max   = 0;

};

template<typename T>
struct TArray {
    T*       Data  = nullptr;
    int32_t  Count = 0;
    int32_t  Max   = 0;

    int32_t Num() const { return Count; }
    bool IsValidIndex(int32_t i) const { return i >= 0 && i < Count; }
};

struct FVector {
    double X = 0.0, Y = 0.0, Z = 0.0;

    FVector operator-(const FVector& o) const { return {X-o.X, Y-o.Y, Z-o.Z}; }
    FVector operator+(const FVector& o) const { return {X+o.X, Y+o.Y, Z+o.Z}; }
    FVector operator*(double s)         const { return {X*s,   Y*s,   Z*s};   }

    double  Dot(const FVector& o)       const { return X*o.X + Y*o.Y + Z*o.Z; }
    double  LengthSq()                  const { return X*X + Y*Y + Z*Z; }
    double  Length()                    const { return sqrt(LengthSq()); }

    FVector Normalized() const {
        double l = Length();
        if (l < 1e-9) return {};
        return {X/l, Y/l, Z/l};
    }
};

struct FVector2D { double X = 0.0, Y = 0.0; };

struct FRotator { double Pitch = 0.0, Yaw = 0.0, Roll = 0.0; };

struct FQuat   { double X=0.0, Y=0.0, Z=0.0, W=1.0; };

struct FTransform {
    FQuat    Rotation;
    FVector  Translation;
    uint8_t  Pad[0x08];
    FVector  Scale3D;
    uint8_t  Pad2[0x08];
};
static_assert(sizeof(FVector) == 0x18);
static_assert(sizeof(FTransform) == 0x60);

struct UObject {
    void*    VTable;
    uint32_t ObjectFlags;
    uint32_t InternalIndex;
    void*    ClassPrivate;
    uint8_t  NamePrivate[8];
    void*    OuterPrivate;
};
static_assert(sizeof(UObject) == 0x28);

struct FUObjectItem {
    UObject* Object;
    uint8_t  Pad[0x10];
};

struct TUObjectArray {
    static constexpr int32_t ElementsPerChunk = 0x10000;
    FUObjectItem** Objects;
    uint8_t        Pad[0x08];
    int32_t        MaxElements;
    int32_t        NumElements;
    int32_t        MaxChunks;
    int32_t        NumChunks;

};
