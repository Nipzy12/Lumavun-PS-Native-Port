# LUMAVUN PS Native Port: execution roadmap

This file is the source of truth for staged development. Every stage must include tests and a working Windows CI build.

| Stage | Deliverable | Acceptance gate | Status |
| --- | --- | --- | --- |
| V0.1 | Native launcher, conservative hardware presets | Windows build and preset tests | Done |
| V0.2 | Game-file import, basic signatures | Import/report tests | Done |
| V0.3 | Disc image analyzer | ISO9660 2048 & raw 2352 parse, PS1/PS2/PS3 candidate tests, malformed input safe | Done (candidate detection only) |
| V0.4 | Recompiler backend interface, compatibility database, build graph, error codes | Demonstrate dry-run and unsupported titles without claiming successful port | Done: control plane only; no native translator |
| V0.5 | First reproducible *actual* native recompilation on authorized small PS1 code/game | Original PS-X EXE MIPS mini-game translated into Windows native C++, gameplay self-test | Done: self-authored demo only; commercial games unsupported |
| V0.6 | PS2 and PS3 backend feasibility | ELF32 MIPS, ELF64 PPU, SPU ELF and SELF structure validation | Partial: header inspector; game-specific function inventory still blocked |
| V0.7 | Native runtime replacement | 2D Win32 GDI + keyboard + save/sound demo on statically translated PS1 homebrew | Partial: original demo only; PS2/PS3 runtimes unavailable |
| V0.8 | Gundam Reborn vertical slice | Native menu + one playable battle scenario, no emulator | Preflight: generic PPU inventory + self-authored PPC64 native translation proof; Gundam Reborn not analyzed or playable |
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

## V0.5 evidence

A self-authored 32-instruction MIPS-I PS-X EXE is generated and statically translated to C++ (no runtime MIPS instruction interpreter), then compiled as `LumavunPS1MiniPort.exe`. This is tested with a 10-turn deterministic native gameplay self-test and deliberate rejection of unsupported opcodes. This is **not** an implementation of PlayStation BIOS, GPU, SPU, generic PS1 games, PS2/PS3 or Gundam Reborn; commercial native port work remains unstarted. See [V0.5 documentation](V0.5-RECOMPILATION.md).

## V0.6 partial implementation

This phase adds a bounded read-only ELF/SELF structure analyzer and machine-readable runtime blocker report. It does not translate PS2/PS3 instructions, enumerate Gundam Reborn functions, decrypt SELF, or provide a native renderer. See [V0.6 feasibility](V0.6-FEASIBILITY.md).

## V0.7 homebrew host milestone

The PS1 demo has a lightweight Win32 GDI graphical host, keyboard controls, user save file with integrity checks, and a Windows system-sound event. It is compiled from the same MIPS-I PS-X EXE as the V0.5 console port. This is NOT a PlayStation hardware graphics/audio runtime, a PS3 renderer, or Gundam Reborn gameplay. See [V0.7 runtime guide](V0.7-NATIVE-RUNTIME.md).

## V0.8 preflight: read-only PPC64 code inventory

The project can now scan a bounded amount of PS3-candidate PPU ELF executable bytes and report branch counts and candidate direct call targets. This is a **static heuristic**, not verified functions, decrypted EBOOT, recompiled PS3 code or playable Gundam Reborn. See [V0.8 PPU inventory](V0.8-PPU-INVENTORY.md). The original V0.8 gameplay acceptance gate remains open.

## V0.8.1 PPC64 native translation proof

A self-authored PowerPC64 ELF demo now undergoes a strict subset of genuine static machine-code-to-C++ translation before the host compiler creates a Windows native executable. A deterministic execution test verifies a numeric result of 15. The source uses a custom **demo-only** syscall ABI and does not support PS3 CellOS, SPU, RSX or Gundam Reborn. All commercial-game build requests remain blocked. See [V0.8.1 PPU recompilation](V0.8.1-PPU-RECOMPILATION.md).

## V0.8.2 checked guest-memory proof

Extended the *original PPC64 demo* static translation with 64-bit/32-bit big-endian loads and stores, byte reads, a deliberately bounded 64 KiB sandbox, alignment and bounds traps. Actual MSVC-compiled homebrew code is tested; retail game builds remain blocked. A mapped PS3 address space, ABI, stack, multiple ELF load segments, CellOS, SPU, RSX, and Gundam Reborn gameplay are **not** implemented. See [V0.8.2 details](V0.8.2-PPU-GUEST-MEMORY.md).
