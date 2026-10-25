# Phantasy Star IV — static recompilation for PC

A native Windows version of *Phantasy Star IV* (Sega Mega Drive), made by statically
recompiling the game's 68000 code to C. Targets the Russian fan translation
(Phantasy Star IV (J) [T+Rus1.1 bybyc9lc9l (23.12.2012)]_fix_v7, SHA-1
`d2820fd0103de2f98b9f742dd076c8897ddd74d0`); any other ROM runs on the built-in interpreter.

**No game data is included.** The recompiled C (`generated/`) is produced from your own ROM.

## Features
- 68000 → C recompiler (~72k instructions, ~1900 functions) with an interpreter fallback
  for code found only at run time (RAM code, handlers called through pointers).
- Own Mega Drive model: VDP with per-line rendering and H-interrupts, Z80, YM2612, PSG.
- Widescreen 16:9 / 16:10 on field maps, painted from the game's own map data; sprite
  culling widened through recompiler patches (`hints.json`).
- HD: 4× edge-directed pixel-art upscaler, multi-threaded.
- Pause menu, rebinding, XInput and DirectInput (WinMM) gamepads, rewind, fast-forward,
  battle speed-up, autosave, save states.

## Verification
- 68000 interpreter: 818k SingleStepTests vectors; Z80: 627k of 630k.
- Recompiled code runs in lockstep with the interpreter (registers + RAM/VRAM/CRAM CRCs
  every frame, 30,000 frames of random play match bit for bit).

## Building
```sh
python3 tools/trace.py rom/ps4_rus.bin hints.json out        # code map
python3 tools/recomp.py rom/ps4_rus.bin out/codemap.json generated
make                                  # Linux headless runner (lockstep tests)
./build_win.sh                        # PS4.exe for Windows (clang + lld-link, no SDK)
```
Tests: `make tests` (68000 vectors), `tests/z80_run.c`, `tests/emu_test.c`, `tests/rw_test.c`.

## Layout
```
src/        CPU interpreter, runtime, Mega Drive model, sound, widescreen, HD, rewind
tools/      tracer, recompiler, disassembler, helpers
win/        Windows front end, minimal libc/Win32 headers, import .def files
tests/      test runners and input scripts
hints.json  extra entry points and recompiler patches
```
This is an unofficial fan project, not affiliated with Sega.
