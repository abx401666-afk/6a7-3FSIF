# /var — Bodycam external overlay

Overlay for Bodycam (PC), built against one specific game build (see `include/Offsets.inc`).

## Build

- Windows 10/11 x64, Visual Studio 2022 with **Desktop development with C++** (MSVC v143, Windows SDK)
- Open `BodycamMod.sln`, select **Release | x64**, build
- Output: `bin\x64\Release\var.exe`

`res/bcvis.dll` is prebuilt from `vistrace/VisTrace.cpp` (MinHook in `vendor/minhook`) with `vistrace/build.sh` (MinGW-w64) and embedded as resource 201. Rebuild it only if you change `VisTrace.cpp` or `include/VisShared.hpp`. It takes all game offsets from var.exe at run time, so the offset updater covers it.

## Run

1. Start Bodycam (borderless or windowed).
2. Start `var.exe`.

`var.exe` exits immediately if the game is not running or is a different build than the one the offsets were taken from.

## Keys

| Key | Action |
|---|---|
| Insert | Menu |

Everything else is set in the menu. Configs are stored in `%APPDATA%\BodycamMod\configs`, logs in `%APPDATA%\BodycamMod\logs`.

## Layout

| Path | Contents |
|---|---|
| `src/Memory.cpp`, `include/Offsets.inc` | Process attach, remote reads, offsets |
| `src/Targets.cpp`, `src/World.cpp` | Player / item / grenade snapshot |
| `src/ESP.cpp`, `src/Skeleton.cpp`, `src/Lens.cpp` | ESP drawing and projection |
| `src/Aimbot.cpp` | Aim assist |
| `src/Visibility.cpp`, `vistrace/` | Line of sight (in-game trace helper) |
| `src/Fx.cpp`, `src/CombatLog.cpp` | Tracers, hit markers, sounds, combat feed |
| `src/Hud.cpp` | HUD widgets |
| `src/Misc.cpp`, `src/Host.cpp` | Misc features |
| `src/Menu.cpp`, `src/UiKit.cpp` | Menu |
| `src/OverlayRenderer.cpp` | D3D12 / DirectComposition overlay window |
