# LUMAVUN PS Native Port: execution roadmap

This file is the source of truth for staged development. Every stage must include tests and a working Windows CI build.

| Stage | Deliverable | Acceptance gate | Status |
| --- | --- | --- | --- |
| V0.1 | Native launcher, conservative hardware presets | Windows build and preset tests | Done |
| V0.2 | Game-file import, basic signatures | Import/report tests | Done |
| V0.3 | Disc image analyzer | ISO9660 2048 & raw 2352 parse, PS1/PS2/PS3 candidate tests, malformed input safe | Done (candidate detection only) |
| V0.4 | Recompiler backend interface, compatibility database, build graph, error codes | Demonstrate dry-run and unsupported titles without claiming successful port | Done: control plane only; no native translator |
| V0.5 | First reproducible *actual* native recompilation on authorized small PS1 code/game | Real translated game code runs as Windows x64 without emulator, gameplay validated | Blocked: choose a feasible source |
| V0.6 | PS2 and PS3 backend feasibility | Game-specific executable/function inventory, graphics/SPU gaps documented | Blocked: lawful test material and toolchain |
| V0.7 | Native runtime replacement | Real renderer/input/audio/save APIs required by selected title | Blocked: V0.6 |
| V0.8 | Gundam Reborn vertical slice | Native menu + one playable battle scenario, no emulator | Research target, no guarantee of feasibility |
| V0.9 | Low-spec optimization | Actual measured FPS, 1% low, memory use and stutter on AMD A4/Radeon R3/4 GB | Blocked: playable game |
| V1.0 | Verified game-port release | Game boots, plays, saves/loads, audio/input and regression tests pass | Blocked: all preceding gates |

## Principles

1. Launcher builds **are not native game ports**. Only a game-specific compiled and verified native runtime can count as a port.
2. Imported ISO, BIN, CUE, ELF, PKG, or PS3_GAME folder is not automatically compiled. No ISO is simply renamed to EXE.
3. We do not ship game binaries, decryption keys, BIOS, proprietary assets, or firmware.
4. Keep each milestone in its own branch and confirm Windows CI before merging.
5. PS4 and PS5 conversion are not currently supported; universal one-click conversion is not an attainable release promise.
6. The low-spec preset is a *starting hypothesis*, not proof of playable FPS.
7. Reuse PS1Recomp, PS2Recomp, or ps3recomp only after per-project license and compatibility validation.

## V0.4 implementation notes

Native pipeline CLI and research-only compatibility database live in src/port_pipeline.hpp, src/port_cli.cpp, and data/compatibility.tsv. The backend contract exists, but no game-specific translator or native runtime is implemented. The build command returns a blocking error until that exists. See [V0.4 contract](V0.4-PIPELINE.md).
