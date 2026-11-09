# Bodycam SDK integration map

Global offsets (Basic.hpp): `GObjects = 0x09AB09F0`, `GNames = 0x099CD240`, `GWorld = 0x09C2C938`, `AppendString = 0x01280880`, `ProcessEvent = 0x014ABDB0`.

| Project use | Offset | SDK class / member |
|---|---:|---|
| World → GameState | `0x160` | `UWorld::GameState` |
| World → GameInstance | `0x1D8` | `UWorld::OwningGameInstance` |
| GameInstance → LocalPlayers | `0x38` | `UGameInstance::LocalPlayers` |
| LocalPlayer → Controller | `0x30` | `UPlayer::PlayerController` |
| Controller → PlayerState | `0x2B0` | `AController::PlayerState` |
| Controller → AcknowledgedPawn | `0x350` | `APlayerController::AcknowledgedPawn` |
| Controller → CameraManager | `0x360` | `APlayerController::PlayerCameraManager` |
| GameState → PlayerArray | `0x2C0` | `AGameStateBase::PlayerArray` |
| PlayerState → Pawn | `0x320` | `APlayerState::PawnPrivate` |
| PlayerState → Name FString | `0x340` | `APlayerState::PlayerNamePrivate` |
| Team (human player pawn) | `0xFF1` | `ABodycam_Player_C::Team` |
| Team (bot pawn) | `0xC98` | `AAI_Humanoid_C::Team` |
| Character → Mesh | `0x328` | `ACharacter::Mesh` |
| Character → Capsule | `0x338` | `ACharacter::CapsuleComponent` |
| Capsule half-height / radius | `0x508` / `0x50C` | `UCapsuleComponent::CapsuleHalfHeight`, `CapsuleRadius` |
| Actor → RootComponent | `0x1B8` | `AActor::RootComponent` |
| Scene component relative location / rotation / scale | `0x128` / `0x140` / `0x158` | `USceneComponent::RelativeLocation`, `RelativeRotation`, `RelativeScale3D` |
| Skeletal component-space transform array | `0x960` | `USkeletalMeshComponent::CachedComponentSpaceTransforms` |
| Camera cache entry | `0x1410` | `APlayerCameraManager::CameraCachePrivate` |
| Player survivor-status component | `0x750` | `AALS_Base_CharacterBP_C::WW_SurvivorStatus` |
| Zombie survivor-status component | `0x6B0` | `AZombie_C::WW_SurvivorStatus` |
| Status death / health / max-health | `0x118` / `0x120` / `0x1B0` | `UWW_SurvivorStatus_C::Death_`, `Health`, `HealthMax` |

The camera cache is read as `FCameraCacheEntry`: `FMinimalViewInfo` begins at `+0x10`; its `Location`, `Rotation`, and `FOV` are consequently at `+0x10`, `+0x28`, and `+0x40` in the entry.

Actor fallback movement data is read from `AActor::ReplicatedMovement` at `0xD0`, with `FRepMovement::Location` at `+0x30` and `Rotation` at `+0x48` (absolute actor offsets `0x100` and `0x118`).
