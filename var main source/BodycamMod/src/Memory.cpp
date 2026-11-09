#include "../include/Memory.hpp"
#include "../include/Skeleton.hpp"

#include <TlHelp32.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
    HANDLE g_process = nullptr;
    DWORD g_pid = 0;
    uintptr_t g_moduleBase = 0;

    bool ContainsInsensitive(const wchar_t* text, const wchar_t* needle) {
        if (!text || !needle) return false;
        std::wstring lhs(text), rhs(needle);
        std::transform(lhs.begin(), lhs.end(), lhs.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        std::transform(rhs.begin(), rhs.end(), rhs.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        return lhs.find(rhs) != std::wstring::npos;
    }

    int GetBodycamProcessPriority(const wchar_t* executableName) {
        if (!executableName || !*executableName ||
            !ContainsInsensitive(executableName, L"bodycam")) {
            return -1;
        }

        if (ContainsInsensitive(executableName, L"crash") ||
            ContainsInsensitive(executableName, L"report") ||
            ContainsInsensitive(executableName, L"launcher") ||
            ContainsInsensitive(executableName, L"bootstrap") ||
            ContainsInsensitive(executableName, L"helper")) {
            return -1;
        }

        if (ContainsInsensitive(executableName, L"bodycam-win64-shipping.exe")) return 1000;
        if (ContainsInsensitive(executableName, L"win64-shipping")) return 900;

        return -1;
    }

    DWORD FindBodycamProcess() {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        DWORD foundPid = 0;
        int bestPriority = -1;

        if (Process32FirstW(snapshot, &entry)) {
            do {
                const int priority = GetBodycamProcessPriority(entry.szExeFile);
                if (priority > bestPriority) {
                    bestPriority = priority;
                    foundPid = entry.th32ProcessID;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return bestPriority >= 900 ? foundPid : 0;
    }

    uintptr_t FindMainModuleBase(DWORD pid) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;

        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        uintptr_t result = 0;
        if (Module32FirstW(snapshot, &entry)) {
            do {
                if (ContainsInsensitive(entry.szModule, L"bodycam") ||
                    ContainsInsensitive(entry.szExePath, L"bodycam")) {
                    result = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                    break;
                }
                if (!result) result = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            } while (Module32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return result;
    }

    struct RemoteArrayHeader {
        uintptr_t data = 0;
        int32_t count = 0;
        int32_t max = 0;
    };

    bool ReadRemoteArrayHeader(uintptr_t address, RemoteArrayHeader& header) {
        if (!ReadRemote(address, header)) return false;
        if (header.count < 0 || header.max < header.count || header.count > 8192) return false;
        if (header.count > 0 && !header.data) return false;
        return true;
    }
}

bool AttachToGameProcess() {
    if (IsGameProcessAttached()) return true;
    DetachFromGameProcess();

    const DWORD pid = FindBodycamProcess();
    if (!pid) return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return false;

    const uintptr_t moduleBase = FindMainModuleBase(pid);
    if (!moduleBase) {
        CloseHandle(process);
        return false;
    }

    g_process = process;
    g_pid = pid;
    g_moduleBase = moduleBase;
    return true;
}

void DetachFromGameProcess() {
    if (g_process) {
        CloseHandle(g_process);
        g_process = nullptr;
    }
    g_pid = 0;
    g_moduleBase = 0;
}

bool IsGameProcessAttached() {
    return g_process != nullptr && g_pid != 0 && g_moduleBase != 0 && IsGameProcessAlive();
}

bool IsGameProcessAlive() {
    if (!g_process) return false;
    DWORD exitCode = 0;
    return GetExitCodeProcess(g_process, &exitCode) && exitCode == STILL_ACTIVE;
}

DWORD GetAttachedProcessId() { return g_pid; }
uintptr_t GetRemoteModuleBase() { return g_moduleBase; }
HANDLE GetRemoteProcessHandle() { return g_process; }

bool ReadRemoteBytes(uintptr_t address, void* destination, size_t size) {
    if (!g_process || !address || !destination || size == 0) return false;
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(g_process, reinterpret_cast<LPCVOID>(address),
                             destination, size, &bytesRead) != FALSE && bytesRead == size;
}

uintptr_t ReadPtr(uintptr_t base, ptrdiff_t offset) {
    if (!base) return 0;
    uintptr_t value = 0;
    const uintptr_t address = static_cast<uintptr_t>(static_cast<intptr_t>(base) + offset);
    return ReadRemote(address, value) ? value : 0;
}

uintptr_t GetWorld() {
    if (!g_moduleBase) return 0;
    return ReadRemoteOr<uintptr_t>(g_moduleBase + Offsets::GWorld);
}

uintptr_t GetLocalPlayerController() {
    const uintptr_t world = GetWorld();
    if (!world) return 0;

    const uintptr_t gameInstance = ReadPtr(world, BodycamOffsets::WorldOwningGameInstance);
    if (!gameInstance) return 0;

    RemoteArrayHeader localPlayers{};
    if (!ReadRemoteArrayHeader(gameInstance + BodycamOffsets::GameInstanceLocalPlayers, localPlayers) ||
        localPlayers.count < 1) return 0;

    uintptr_t localPlayer = 0;
    if (!ReadRemote(localPlayers.data, localPlayer) || !localPlayer) return 0;
    return ReadPtr(localPlayer, BodycamOffsets::LocalPlayerController);
}

uintptr_t GetLocalPlayerState() {
    const uintptr_t controller = GetLocalPlayerController();
    return controller ? ReadPtr(controller, BodycamOffsets::ControllerPlayerState) : 0;
}

uintptr_t GetLocalPawn() {
    const uintptr_t controller = GetLocalPlayerController();
    if (!controller) return 0;

    uintptr_t pawn = ReadPtr(controller, BodycamOffsets::ControllerAcknowledgedPawn);
    if (pawn) return pawn;

    const uintptr_t playerState = GetLocalPlayerState();
    return playerState ? GetPawnFromPlayerState(playerState) : 0;
}

uintptr_t GetCameraManager() {
    const uintptr_t controller = GetLocalPlayerController();
    return controller ? ReadPtr(controller, BodycamOffsets::ControllerCameraManager) : 0;
}

uintptr_t GetGameState() {
    const uintptr_t world = GetWorld();
    return world ? ReadPtr(world, BodycamOffsets::WorldGameState) : 0;
}

uintptr_t GetSurvivorStatus(uintptr_t pawn) {
    if (!pawn) return 0;

    const ptrdiff_t candidates[] = {
        BodycamOffsets::PlayerSurvivorStatus,
        BodycamOffsets::ZombieSurvivorStatus
    };

    for (const ptrdiff_t offset : candidates) {
        const uintptr_t status = ReadPtr(pawn, offset);
        if (!status) continue;

        double health = 0.0;
        double maxHealth = 0.0;
        uint8_t death = 0xFF;
        if (!ReadRemote(status + BodycamOffsets::SurvivorStatusHealth, health) ||
            !ReadRemote(status + BodycamOffsets::SurvivorStatusMaxHealth, maxHealth) ||
            !ReadRemote(status + BodycamOffsets::SurvivorStatusDeath, death)) {
            continue;
        }

        if (death <= 1 && std::isfinite(health) && std::isfinite(maxHealth) &&
            maxHealth > 0.0 && maxHealth < 100000.0 &&
            health >= -100000.0 && health < 1000000.0) {
            return status;
        }
    }
    return 0;
}

uintptr_t GetPawnFromPlayerState(uintptr_t playerState) {
    return playerState ? ReadPtr(playerState, BodycamOffsets::PlayerStatePawn) : 0;
}

FCameraCache GetCameraCache() {
    FCameraCache cache{};
    const uintptr_t manager = GetCameraManager();
    if (!manager) return cache;

    const int64_t pov = OFF(CameraPOV);
    const int64_t lo = pov + (std::min)({ OFF(ViewLocation), OFF(ViewRotation), OFF(ViewFOV) });
    const int64_t hi = pov + (std::max)({ OFF(ViewLocation) + 24, OFF(ViewRotation) + 24, OFF(ViewFOV) + 4 });
    uint8_t buf[0x200];
    if (lo < 0 || hi - lo > static_cast<int64_t>(sizeof(buf)) ||
        !ReadRemoteBytes(manager + BodycamOffsets::CameraCache + lo, buf, static_cast<size_t>(hi - lo))) return cache;
    std::memcpy(&cache.Location, buf + (pov + OFF(ViewLocation) - lo), sizeof(cache.Location));
    std::memcpy(&cache.Rotation, buf + (pov + OFF(ViewRotation) - lo), sizeof(cache.Rotation));
    std::memcpy(&cache.FOV, buf + (pov + OFF(ViewFOV) - lo), sizeof(cache.FOV));
    return cache;
}

bool GetControlRotation(FRotator& rotation) {
    const uintptr_t controller = GetLocalPlayerController();
    if (!controller) return false;
    FRotator value{};
    if (!ReadRemote(controller + BodycamOffsets::ControllerControlRotation, value)) return false;
    if (!std::isfinite(value.Pitch) || !std::isfinite(value.Yaw) || !std::isfinite(value.Roll)) return false;
    rotation = value;
    return true;
}

bool IsLocalPawnAiming() {
    const uintptr_t pawn = GetLocalPawn();
    return pawn && ReadRemoteOr<uint8_t>(pawn + BodycamOffsets::BodycamPlayerAiming, 0) != 0;
}

std::vector<uintptr_t> GetPlayerArray() {
    static std::vector<uintptr_t> lastGood;
    static ULONGLONG lastGoodTick = 0;
    constexpr ULONGLONG kStaleMs = 400;
    auto fallback = [&]() -> std::vector<uintptr_t> {
        return (GetTickCount64() - lastGoodTick < kStaleMs) ? lastGood : std::vector<uintptr_t>{};
    };

    const uintptr_t gameState = GetGameState();
    if (!gameState) return fallback();

    const uintptr_t arrayAddress = gameState + BodycamOffsets::PlayerArray;
    for (int attempt = 0; attempt < 3; ++attempt) {
        RemoteArrayHeader before{};
        if (!ReadRemoteArrayHeader(arrayAddress, before)) {
            Sleep(0);
            continue;
        }

        if (before.count == 0) {
            RemoteArrayHeader after{};
            if (ReadRemoteArrayHeader(arrayAddress, after) &&
                after.data == before.data && after.count == 0) {
                lastGood.clear();
                lastGoodTick = GetTickCount64();
                return {};
            }
            Sleep(0);
            continue;
        }

        std::vector<uintptr_t> result(static_cast<size_t>(before.count));
        if (!ReadRemoteBytes(before.data, result.data(), result.size() * sizeof(uintptr_t))) {
            Sleep(0);
            continue;
        }

        RemoteArrayHeader after{};
        if (!ReadRemoteArrayHeader(arrayAddress, after) ||
            after.data != before.data || after.count != before.count) {
            Sleep(0);
            continue;
        }

        result.erase(std::remove(result.begin(), result.end(), uintptr_t{0}), result.end());
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        lastGood = result;
        lastGoodTick = GetTickCount64();
        return result;
    }
    return fallback();
}

namespace {
    bool IsDroneActor(uintptr_t actor) {
        if (!actor) return false;
        const uintptr_t components[3] = { ReadPtr(actor, OFF(DroneCamera)), ReadPtr(actor, OFF(DroneMovement)),
                                          ReadPtr(actor, OFF(DroneHitSphere)) };
        for (const uintptr_t component : components) {
            if (!component) return false;
            if (ReadPtr(component, BodycamOffsets::ObjectOuter) != actor) return false;
        }
        return true;
    }

    bool OwnsComponent(uintptr_t actor, ptrdiff_t slotOffset) {
        const uintptr_t component = ReadPtr(actor, slotOffset);
        return component && ReadPtr(component, BodycamOffsets::ObjectOuter) == actor;
    }

    bool HasCharacterComponents(uintptr_t actor) {
        return actor && OwnsComponent(actor, BodycamOffsets::CharacterMesh) &&
               OwnsComponent(actor, BodycamOffsets::CharacterCapsule);
    }

    bool IsHumanPlayerPawn(uintptr_t pawn) {
        return pawn && OwnsComponent(pawn, BodycamOffsets::BodycamPlayerCamera);
    }

    struct DroneEntry { uintptr_t actor = 0; size_t index = 0; };
    std::vector<DroneEntry> g_droneCache;

    constexpr int32_t kDeadObjectFlags = 0x00008000 | 0x00010000 | 0x40000000;
    bool IsObjectDying(uintptr_t object) {
        int32_t flags = 0;
        if (!ReadRemote(object + OFF(ObjectFlags), flags)) return true;
        return (flags & kDeadObjectFlags) != 0;
    }

    bool IsDroneDead(uintptr_t drone) {
        uint8_t destroyFlag = 0;
        if (ReadRemote(drone + OFF(DroneDestroyFlag), destroyFlag) && destroyFlag != 0) return true;
        const uintptr_t survivor = ReadPtr(drone, OFF(DroneSurvivor));
        if (survivor && ReadPtr(survivor, BodycamOffsets::ObjectOuter) == drone) {
            uint8_t deathState = 0;
            if (ReadRemote(survivor + OFF(SurvivorDeathState), deathState) && deathState != 0) return true;
        }
        return false;
    }

    void PruneDeadDrones() {
        if (g_droneCache.empty()) return;

        uintptr_t slots = 0;
        int32_t slotCount = 0;
        const uintptr_t world = GetWorld();
        const uintptr_t level = world ? ReadPtr(world, BodycamOffsets::WorldPersistentLevel) : 0;
        RemoteArrayHeader header{};
        if (level && ReadRemoteArrayHeader(level + BodycamOffsets::LevelActors, header)) {
            slots = header.data;
            slotCount = header.count;
        }

        g_droneCache.erase(std::remove_if(g_droneCache.begin(), g_droneCache.end(),
            [&](const DroneEntry& drone) {
                if (!IsDroneActor(drone.actor) || IsObjectDying(drone.actor) || IsDroneDead(drone.actor)) return true;
                if (slots && drone.index < static_cast<size_t>(slotCount)) {
                    uintptr_t slot = 0;
                    if (ReadRemote(slots + drone.index * sizeof(uintptr_t), slot) && slot != drone.actor) return true;
                }
                return false;
            }), g_droneCache.end());
    }
    std::unordered_map<uintptr_t, uintptr_t> g_scannedChars;
    std::unordered_set<uintptr_t> g_knownCharClasses;
    std::unordered_set<uintptr_t> g_droneClasses;
    std::unordered_map<uintptr_t, ULONGLONG> g_rejectedClasses;
    std::vector<uintptr_t> g_scanActors;
    size_t g_scanIndex = 0;
    ULONGLONG g_lastScanMs = 0;
    constexpr size_t kScanSlice = 800;
    constexpr size_t kMaxScanActors = 200000;
    constexpr ULONGLONG kScanIntervalMs = 50;
    constexpr ULONGLONG kRejectRecheckMs = 10000;

    void AdvanceActorScan() {
        const ULONGLONG now = GetTickCount64();
        if (now - g_lastScanMs < kScanIntervalMs) return;
        g_lastScanMs = now;

        for (auto it = g_scannedChars.begin(); it != g_scannedChars.end();) {
            const bool alive = ReadPtr(it->first, 0x10) == it->second && HasCharacterComponents(it->first);
            it = alive ? std::next(it) : g_scannedChars.erase(it);
        }

        if (g_scanIndex >= g_scanActors.size()) {
            g_scanActors.clear();
            g_scanIndex = 0;
            const uintptr_t world = GetWorld();
            const uintptr_t level = world ? ReadPtr(world, BodycamOffsets::WorldPersistentLevel) : 0;
            RemoteArrayHeader header{};
            if (level && ReadRemoteArrayHeader(level + BodycamOffsets::LevelActors, header) &&
                header.count > 0 && static_cast<size_t>(header.count) <= kMaxScanActors) {
                std::vector<uintptr_t> actors(static_cast<size_t>(header.count));
                if (ReadRemoteBytes(header.data, actors.data(), actors.size() * sizeof(uintptr_t))) {
                    g_scanActors = std::move(actors);
                }
            }
        }

        const size_t end = (std::min)(g_scanActors.size(), g_scanIndex + kScanSlice);
        for (size_t i = g_scanIndex; i < end; ++i) {
            const uintptr_t actor = g_scanActors[i];
            if (!actor) continue;
            const uintptr_t cls = ReadPtr(actor, OFF(ObjectClass));
            if (!cls) continue;

            if (g_knownCharClasses.count(cls)) {
                if (!g_scannedChars.count(actor) && HasCharacterComponents(actor))
                    g_scannedChars[actor] = cls;
                continue;
            }
            if (g_droneClasses.count(cls)) {
                const bool known = std::any_of(g_droneCache.begin(), g_droneCache.end(),
                    [&](const DroneEntry& d) { return d.actor == actor; });
                if (!known && IsDroneActor(actor) && !IsObjectDying(actor) && !IsDroneDead(actor))
                    g_droneCache.push_back({ actor, i });
                continue;
            }
            const auto rejected = g_rejectedClasses.find(cls);
            if (rejected != g_rejectedClasses.end() && now < rejected->second) continue;

            if (IsDroneActor(actor)) {
                g_droneClasses.insert(cls);
                if (!IsObjectDying(actor) && !IsDroneDead(actor)) g_droneCache.push_back({ actor, i });
            } else {
                g_rejectedClasses[cls] = now + kRejectRecheckMs;
            }
        }
        g_scanIndex = end;
    }
}

std::vector<uintptr_t> GetDronePawns() {
    AdvanceActorScan();
    PruneDeadDrones();
    std::vector<uintptr_t> drones;
    drones.reserve(g_droneCache.size());
    for (const DroneEntry& drone : g_droneCache) drones.push_back(drone.actor);
    return drones;
}

std::vector<PawnEntry> GetCharacterPawns() {
    static std::vector<PawnEntry> lastGood;
    static ULONGLONG lastGoodTick = 0;
    constexpr ULONGLONG kStaleMs = 400;

    std::vector<PawnEntry> out;
    std::unordered_set<uintptr_t> seen;

    for (const uintptr_t playerState : GetPlayerArray()) {
        const uintptr_t pawn = GetPawnFromPlayerState(playerState);
        if (!pawn || !seen.insert(pawn).second) continue;
        out.push_back({ pawn, playerState });

        if (HasCharacterComponents(pawn)) {
            const uintptr_t cls = ReadPtr(pawn, OFF(ObjectClass));
            if (cls) g_knownCharClasses.insert(cls);
        }
    }

    AdvanceActorScan();
    for (const auto& scanned : g_scannedChars) {
        if (seen.insert(scanned.first).second) out.push_back({ scanned.first, 0 });
    }

    const ULONGLONG now = GetTickCount64();
    if (!out.empty()) {
        lastGood = out;
        lastGoodTick = now;
        return out;
    }
    return (now - lastGoodTick < kStaleMs) ? lastGood : out;
}

float GetWorldTimeSeconds() {
    const uintptr_t manager = GetCameraManager();
    if (!manager) return 0.0f;
    float time = 0.0f;
    if (!ReadRemote(manager + BodycamOffsets::CameraCache, time) || !std::isfinite(time)) return 0.0f;
    return time;
}

bool GetCapsuleDimensions(uintptr_t actor, float& radius, float& halfHeight) {
    const float fallbackRadius = radius;
    const float fallbackHalfHeight = halfHeight;
    if (!actor) return false;

    const uintptr_t capsule = ReadPtr(actor, BodycamOffsets::CharacterCapsule);
    if (!capsule) return false;

    float rawRadius = 0.0f;
    float rawHalfHeight = 0.0f;
    FVector scale{1.0, 1.0, 1.0};
    if (!ReadRemote(capsule + BodycamOffsets::CapsuleRadius, rawRadius) ||
        !ReadRemote(capsule + BodycamOffsets::CapsuleHalfHeight, rawHalfHeight)) {
        radius = fallbackRadius;
        halfHeight = fallbackHalfHeight;
        return false;
    }
    ReadRemote(capsule + BodycamOffsets::SceneRelativeScale, scale);

    const double xyScale = (std::max)(std::abs(scale.X), std::abs(scale.Y));
    const double zScale = (std::max)(xyScale, std::abs(scale.Z));
    const float resolvedRadius = static_cast<float>(rawRadius * xyScale);
    const float resolvedHalfHeight = static_cast<float>(rawHalfHeight * zScale);
    if (!std::isfinite(resolvedRadius) || !std::isfinite(resolvedHalfHeight) ||
        resolvedRadius < 10.0f || resolvedRadius > 150.0f ||
        resolvedHalfHeight < resolvedRadius || resolvedHalfHeight > 300.0f) {
        radius = fallbackRadius;
        halfHeight = fallbackHalfHeight;
        return false;
    }

    radius = resolvedRadius;
    halfHeight = resolvedHalfHeight;
    return true;
}

FVector GetActorLocation(uintptr_t actor) {
    if (!actor) return {};

    const uintptr_t root = ReadPtr(actor, BodycamOffsets::ActorRootComponent);
    FVector location{};
    if (root && ReadRemote(root + BodycamOffsets::SceneRelativeLocation, location) &&
        std::isfinite(location.X) && std::isfinite(location.Y) && std::isfinite(location.Z) &&
        location.LengthSq() > 1.0) {
        return location;
    }

    FVector replicated{};
    if (ReadRemote(actor + BodycamOffsets::ActorReplicatedLocation, replicated) &&
        std::isfinite(replicated.X) && std::isfinite(replicated.Y) &&
        std::isfinite(replicated.Z) && replicated.LengthSq() > 1.0) {
        return replicated;
    }
    return {};
}

FRotator GetActorRotation(uintptr_t actor) {
    if (!actor) return {};
    const uintptr_t root = ReadPtr(actor, BodycamOffsets::ActorRootComponent);
    FRotator rotation{};
    if (root && ReadRemote(root + BodycamOffsets::SceneRelativeRotation, rotation) &&
        std::isfinite(rotation.Pitch) && std::isfinite(rotation.Yaw) &&
        std::isfinite(rotation.Roll)) {
        return rotation;
    }
    ReadRemote(actor + BodycamOffsets::ActorReplicatedRotation, rotation);
    return rotation;
}

uintptr_t GetMeshComponent(uintptr_t pawn) {
    return pawn ? ReadPtr(pawn, BodycamOffsets::CharacterMesh) : 0;
}

bool IsLikelyDronePawn(uintptr_t pawn) {
    return IsDroneActor(pawn);
}

namespace {
}

std::string GetPlayerName(uintptr_t playerState) {
    if (!playerState) return {};

    RemoteArrayHeader header{};
    if (!ReadRemoteArrayHeader(playerState + BodycamOffsets::PlayerStateName, header) ||
        header.count <= 1 || header.count > 257) return {};

    std::vector<wchar_t> buffer(static_cast<size_t>(header.count), L'\0');
    if (!ReadRemoteBytes(header.data, buffer.data(), buffer.size() * sizeof(wchar_t))) return {};
    const int length = header.count - 1;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer.data(), length, result.data(), bytes, nullptr, nullptr);
    return result;
}

double GetPlayerHealth(uintptr_t pawn) {
    const uintptr_t status = GetSurvivorStatus(pawn);
    return status ? ReadRemoteOr<double>(status + BodycamOffsets::SurvivorStatusHealth, -1.0) : -1.0;
}

bool GetPlayerHealthLive(uintptr_t pawn, double& health, double& maxHealth) {
    health = -1.0; maxHealth = -1.0;
    const uintptr_t status = GetSurvivorStatus(pawn);
    if (!status) return false;
    const double bpMax = ReadRemoteOr<double>(status + BodycamOffsets::SurvivorStatusMaxHealth, -1.0);
    const double bpHp = ReadRemoteOr<double>(status + BodycamOffsets::SurvivorStatusHealth, -1.0);
    health = bpHp; maxHealth = bpMax;
    const uintptr_t attrs = ReadPtr(status, OFF(SurvivorAttributeSet));
    if (!attrs) return true;
    float cur = -1.0f, mx = -1.0f;
    if (!ReadRemote(attrs + OFF(AttrHealthCurrent), cur) || !ReadRemote(attrs + OFF(AttrMaxHealthCurrent), mx)) return true;
    if (!std::isfinite(cur) || !std::isfinite(mx) || mx < 1.0f || mx > 5000.0f || cur < -1.0f || cur > mx + 0.5f) return true;
    if (bpMax >= 1.0 && bpMax < 5000.0 && std::fabs(bpMax - mx) > bpMax * 0.02) return true;
    health = (std::max)(0.0, static_cast<double>(cur));
    maxHealth = mx;
    return true;
}

double GetPlayerMaxHealth(uintptr_t pawn) {
    const uintptr_t status = GetSurvivorStatus(pawn);
    return status ? ReadRemoteOr<double>(status + BodycamOffsets::SurvivorStatusMaxHealth, -1.0) : -1.0;
}

namespace {
    constexpr uint8_t kUnknownTeam = 0xFF;
    constexpr uint8_t kMaxPlausibleTeam = 31;
}

uint8_t GetPawnTeam(uintptr_t pawn) {
    if (!pawn || !HasCharacterComponents(pawn)) return kUnknownTeam;

    int kind = 0;
    for (const std::string& n : Skeleton_ClassChain(pawn)) {
        if (n.find("ai_humanoid") != std::string::npos) { kind = 2; break; }
        if (n.find("bodycam_player") != std::string::npos) { kind = 1; break; }
    }
    if (kind == 0) kind = IsHumanPlayerPawn(pawn) ? 1 : 2;
    const ptrdiff_t teamOffset = kind == 1 ? BodycamOffsets::BodycamPlayerTeam : BodycamOffsets::AIHumanoidTeam;
    uint8_t team = kUnknownTeam;
    if (ReadRemote(pawn + teamOffset, team) && team <= kMaxPlausibleTeam) return team;
    return kUnknownTeam;
}

uint8_t GetLocalTeam() {
    static uint8_t lastGood = kUnknownTeam;

    uint8_t team = kUnknownTeam;
    if (const uintptr_t pawn = GetLocalPawn()) team = GetPawnTeam(pawn);
    if (team == kUnknownTeam) {
        if (const uintptr_t controller = GetLocalPlayerController()) {
            uint8_t value = kUnknownTeam;
            if (ReadRemote(controller + BodycamOffsets::BodycamPlayerControllerTeam, value) &&
                value <= kMaxPlausibleTeam)
                team = value;
        }
    }
    if (team != kUnknownTeam) lastGood = team;
    return lastGood;
}

bool IsBotPawn(uintptr_t pawn) {
    return pawn && HasCharacterComponents(pawn) && !IsHumanPlayerPawn(pawn);
}

bool IsPlayerDead(uintptr_t pawn) {
    const uintptr_t status = GetSurvivorStatus(pawn);
    if (!status) return false;

    bool death = false;
    double health = 0.0;
    if (!ReadRemote(status + BodycamOffsets::SurvivorStatusDeath, death)) return false;
    if (!ReadRemote(status + BodycamOffsets::SurvivorStatusHealth, health)) return death;
    return death || (std::isfinite(health) && health <= 0.0);
}

bool IsObjectAlive(uintptr_t object) {
    if (object <= 0x10000 || object >= 0x7FFFFFFFFFFFull || (object & 7)) return false;
    uintptr_t cls = 0;
    if (!ReadRemote(object + OFF(ObjectClass), cls) || cls <= 0x10000 || cls >= 0x7FFFFFFFFFFFull) return false;
    return !IsObjectDying(object);
}
