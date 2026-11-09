# /var — Bodycam external overlay

Overlay for Bodycam (PC), built against one specific game build (see `include/Offsets.inc`).

## Dependencies

Third-party source is supplied through Git submodules:

- Dear ImGui v1.93.0 at `vendor/imgui`
- MinHook at `vendor/minhook`

When preparing this source ZIP for your own GitHub repository, extract it, open PowerShell in this folder, and run:

```powershell
.\setup-submodules.ps1
git add .
git status
git commit -m "Prepare /var source with Git submodules"
```

The setup script creates the real Git submodule entries. Do not manually copy third-party library files back into `vendor/`.

For a normal clone after the repository has been pushed:

```bash
git clone --recurse-submodules YOUR_REPOSITORY_URL
```

For tooling that explicitly wants the latest upstream branch revisions (as requested by ExLoader):

```bash
git clone --recurse-submodules --remote-submodules YOUR_REPOSITORY_URL
```

## Build

- Windows 10/11 x64, Visual Studio 2022 with **Desktop development with C++** (MSVC v143, Windows SDK)
- MinGW-w64 cross-compiler tools (`x86_64-w64-mingw32-gcc` and `x86_64-w64-mingw32-g++`) available on `PATH` for the helper DLL build
- Initialize submodules before building

First build the generated helper DLL from the project root using Git Bash or another POSIX shell with the MinGW-w64 cross-compiler installed:

```bash
bash vistrace/build.sh
```

This generates `res/bcvis.dll`. The binary is intentionally ignored by Git; `res/var.rc` embeds it as resource 201 when Visual Studio builds the executable.

Then open `BodycamMod.sln`, select **Release | x64**, and build. Output: `bin\x64\Release\var.exe`.

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
