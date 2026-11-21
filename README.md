# LUMAVUN PS Native Port

Repository for an experimental Windows-native PlayStation porting toolkit and game launchers.

**V0.1 scope: native Windows launcher only.** This is not an emulator and does not yet convert PlayStation games into Windows games.

## Launcher features
- Detect local CPU, GPU, RAM, and recommend a conservative graphics profile.
- Set resolution, render scale, FPS limit, and fullscreen.
- Save settings to LocalAppData.
- Play only enables when a plausible native Windows x64 Game/GameCore.exe exists.
- Build on GitHub Actions; native runtime and per-game recompilation are future work.

## Windows build
Install Visual Studio 2022 C++ tools and CMake, then run:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Binary: build/Release/LumavunPSLauncher.exe.

For AMD A4 / Radeon R3 / 4 GB RAM, the suggested starting preset is 960x540, 70% internal scale and 30 FPS. It is **not a guaranteed measured frame rate**.

This repository does not contain copyrighted games, keys or firmware.

## V0.2 Game inspection
The launcher includes **Import Game File**, **Import Game Folder**, and **Save Report**. The separate `lumavun_inspect` command analyzes simple PS1/PS2/PS3 executable signatures and game folder structures. It does not run a native recompiler or create playable ports. ISO and package parsing are not implemented. Gundam Reborn is not yet ported.
