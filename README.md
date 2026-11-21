# LUMAVUN PS Native Port

A Windows-native launcher and experimental PlayStation porting workspace.

**Status:** launcher prototype only. This repository does **not** currently convert PS1–PS5 games into working Windows executables. Each game requires compatible recompilation, replacement system services and game-specific validation.

## Goals

- Windows x64 native launcher with Settings and Play Game.
- Automatic CPU/GPU/RAM detection and conservative low-spec graphics recommendations.
- Per-game profiles and adjustable settings stored in JSON.
- Disable Play until a real native GameCore.exe is available.
- PS1/PS2/PS3 analysis adapters as experimental future projects, never emulator wrappers presented as native ports.
- Never distribute copyrighted game content or encryption keys.

### Target hardware

AMD A4 / Radeon R3 / 4 GB RAM: start at 960x540 output, 70% render scale, 30 FPS limit, low effects/textures, shadows off. This is a starting suggestion, **not a tested performance guarantee**.

### Build

Requires Windows 10/11 x64, MSVC 2022 C++ desktop tools and CMake.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

If CI passes, download `LumavunPSLauncher-Windows-x64` from GitHub Actions artifacts.
