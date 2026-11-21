# Native port roadmap

V0.1 is a functional launcher source prototype, not a PlayStation game converter.

The launcher detects Windows hardware, suggests safe presets and saves a JSON config.
Its Play button accepts only a plausible x64 Windows executable at Game/GameCore.exe.
Even a valid PE header does not guarantee that a native port works.

Next:
1. Build source inventory and versioned per-game profiles.
2. Explore appropriate per-platform recompilers for PS1, PS2 and PS3.
3. Implement replacement native runtime per game: graphics, audio, controller, save data, system APIs.
4. Validate playability, FPS percentiles, memory pressure, crashes and input on target low-spec hardware.

No assumption that PS4 and PS5 games are universally convertible.
Do not include copyrighted game binaries, assets, encryption keys or system firmware in this repository.

## V0.2 Stage: file importer
Recognizes PS-X EXE, MIPS / PPC ELF, PS3_GAME folder, PS2 SYSTEM.CNF, and flags ISO/BIN/CUE/PKG for further parsing. **Not recompilation.** A playable Gundam Reborn port needs decrypted PS3 game executable, SPU/GPU/audio runtime implementation and per-game development; see https://github.com/sp00nznet/ps3recomp.
