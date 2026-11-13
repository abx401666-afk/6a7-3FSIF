# /var — Bodycam external overlay

This repository contains the `/var` C++ source project, its pinned Git submodules, and the distributed Windows executables.

## Clone with dependencies

```powershell
git clone --recurse-submodules https://github.com/abx401666-afk/6a7-3FSIF.git
```

If the repository was already cloned without submodules:

```powershell
git submodule update --init --recursive
```

## Source and build instructions

See [`var/README.md`](var/README.md) for the Visual Studio project, required Windows toolchain, and the `bcvis.dll` helper build instructions.

Third-party dependency revisions and licenses are documented in [`var/DEPENDENCIES.md`](var/DEPENDENCIES.md).

## Automated build

The `Windows build` GitHub Actions workflow checks out both submodules, builds the generated `bcvis.dll` helper with MinGW-w64, and builds the Release x64 Visual Studio solution with MSBuild. It uploads the build outputs as a workflow artifact.

## Usage scope

Use the project only in offline bot matches. The repository includes a separate multiplayer-blocker executable; the presence of that binary in the repository is not, by itself, a verification of its runtime behavior.
