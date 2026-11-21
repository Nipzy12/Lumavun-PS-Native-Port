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
The launcher includes **Import Game File**, **Import Game Folder**, and **Save Report**. The separate `lumavun_inspect` command analyzes simple PS1/PS2/PS3 executable signatures and game folder structures. It does not run a native recompiler or create playable ports. At V0.2, ISO and package parsing were not implemented. Gundam Reborn is not yet ported.

## V0.3: ISO9660 candidate detection (in progress)
The import analyzer now inspects ISO9660 Primary Volume Descriptors and bounded root-directory metadata from 2048-byte data-sector images and raw 2352-byte Mode1 images. It identifies PS1/PS2 candidates using SYSTEM.CNF (BOOT/BOOT2) and PS3 candidates using PS3_GAME. It does **not** decrypt, emulate, recompile, or port these images. Unsupported ISO variants remain ambiguous. Disc scanning reads at most 1 MiB of directory records and small metadata buffers, avoiding loading a full game image into RAM.

See [staged roadmap](docs/ROADMAP.md) for acceptance criteria, known blockers, and next work.

## V0.4 Porting pipeline (experimental)

The build manager now provides a typed backend interface and a safe, machine-readable compatibility check.
The Windows artifact includes lumavun_port.exe and data/compatibility.tsv. Run the porting CLI
with backends, compatibility, plan, or build. The build command intentionally rejects unsupported
titles, so no user receives a fake port executable. See [V0.4 guide](docs/V0.4-PIPELINE.md).

**A working game-specific native translator is still missing.** Gundam Reborn remains research-only.

## V0.5: PS1 homebrew static recompilation demonstration

This is the **first actual source-code translation proof**, but only for an original,
small PS-X EXE / MIPS-I game authored in this repository. CMake generates the PS-X EXE,
statically translates supported MIPS instructions into native C++, and builds
`LumavunPS1MiniPort.exe`. Run it directly or click
**Play Native PS1 Homebrew Demo** in the Windows launcher. Press A/D then Enter;
collect the star in a ten-turn game.

The compiled native demo is **not** a port of Gundam Reborn, a commercial PS1
title, or arbitrary ISO files. The custom demonstration syscall ABI is not
PlayStation BIOS compatibility. Unsupported MIPS instructions fail closed.

See [V0.5 documentation](docs/V0.5-RECOMPILATION.md).

## V0.6 PS2/PS3 feasibility (partial)

After Import Game, click **Analyze PS2/PS3 ELF or SELF**, or use `lumavun_probe.exe <path>` to inspect an ELF, SCE SELF or PS3_GAME folder. A read-only JSON report records ELF segment structure, candidate CPU family, and unavailable runtime components. This is metadata analysis, not native recompilation or proof of Gundam Reborn compatibility. See [V0.6 guide](docs/V0.6-FEASIBILITY.md).

## V0.7 native Win32 graphical host (homebrew only)

The same original PS1 MIPS-I demo now compiles into a lightweight native windowed program: **LumavunPS1MiniPortGui.exe**. Click **Play Recompiled PS1 Graphical Demo** in the launcher. A/D (or arrows) move, Space stays, F5 saves, F9 loads, Escape exits. Uses GDI and Windows keyboard events; no emulator. Limited homebrew demo only, **not** a renderer for PS3 or playable Gundam Reborn. [Technical details](docs/V0.7-NATIVE-RUNTIME.md).

## V0.8 preflight: PS3 PPU executable analysis

Use `lumavun_ppu_scan.exe <plaintext-PPU-ELF> --report ppu.json` or **Analyze PS3 PPU Code (Read Only)** in the launcher. This tool scans executable PPC64 code in small chunks, counts branch instructions and returns bounded samples of candidate call targets. It rejects ELF-less SELF containers, does not decrypt, and never generates a game executable. [Limitations and test criteria](docs/V0.8-PPU-INVENTORY.md). Gundam Reborn is not yet analyzed or ported.

## V0.8.1 PPC64 ELF -> Windows native proof (homebrew only)

GitHub Actions now builds `LumavunPPUHomebrewPort.exe` from a real, original **ELF64 PowerPC machine-code file** using a strict translator and compiled C++ output. It prints `PPU_HOMEBREW_RESULT=15` when run. The workflow artifact includes `PPU_HOMEBREW.ELF`, generated `LumavunPPUHomebrewPort.cpp`, and the native EXE. This is **not** a PS3 retail game port and the translator refuses unsupported instructions/SELF. See [V0.8.1 documentation](docs/V0.8.1-PPU-RECOMPILATION.md).

## V0.8.2: checked PPC64 guest-memory instructions (homebrew demo only)

The native PPC64 demo now performs actual `std/ld/stw/lwz/lbz` operations in a **64 KiB simulated big-endian guest RAM** and prints `PPU_GUEST_BE_BYTES=18,52` alongside `PPU_HOMEBREW_RESULT=15`. Invalid guest addresses and alignment are rejected with a defined exit code. The Windows CI tests the safe path and native memory traps. There is still **no** retail PS3 memory mapping, CellOS, SPU, RSX, game recompilation, or playable Gundam Reborn. See [V0.8.2 guest memory](docs/V0.8.2-PPU-GUEST-MEMORY.md).

## V0.8.3: nested PPC64 calls and guest stack (homebrew-only)

The original PowerPC64 ELF now uses two levels of `bl` calls, `mflr/mtlr`, and
`stdu` guest stack frames with big-endian backchains. The native executable
verifies its two frames and LR were restored by printing `PPU_STACK_RESTORED=1`.
Out-of-bounds/misaligned guest stacks return exit code 12. The generic game
port command is still blocked; this is not PS3 CellOS, ELF relocations or
Gundam Reborn. [Technical notes](docs/V0.8.3-PPU-STACK.md).

## V0.8.4: multi-segment PPC64 ELF and bounded RELA relocation

Our self-authored PPC64 homebrew ELF now has separate executable and initialized RW data/BSS PT_LOAD segments. A checked `R_PPC64_RELATIVE` (type 22) relocation patches a guest pointer before the statically translated instructions run. The native output adds `PPU_ELF_RELOCATED=1` only after verifying relocated data, an initialized byte, and zero-filled BSS. Malformed sections and unsupported relocations fail closed. This is **not** dynamic linking, a full PS3 memory loader or a Gundam Reborn port. See [V0.8.4 technical guide](docs/V0.8.4-ELF-RELOCATIONS.md).
