# Phantasy Star IV — static recompilation for PC

A native Windows version of *Phantasy Star IV* (Sega Mega Drive), made by statically
recompiling the game's 68000 code to C. Targets the Russian fan translation
(Phantasy Star IV (J) [T+Rus1.1 bybyc9lc9l (23.12.2012)]_fix_v7, SHA-1
`d2820fd0103de2f98b9f742dd076c8897ddd74d0`); any other ROM runs on the built-in interpreter.

**No game data is included.** The recompiled C (`generated/`) is produced from your own ROM.

## Two builds
- **PS4.exe from the release** contains no game code: it runs your ROM on the built-in
  68000 interpreter, with every feature below (output identical to the recompiled build,
  frame for frame, in the tests). Build it with `FREE=1 ./build_win.sh`.
- **The recompiled build** (`./build_win.sh` after generating `generated/` from your ROM)
  runs the game as native code. It contains code translated from the ROM, so it is
  never published.

## Features
- 68000 → C recompiler (~72k instructions, ~1900 functions) with an interpreter fallback
  for code found only at run time (RAM code, handlers called through pointers).
- Own Mega Drive model: VDP with per-line rendering and H-interrupts, Z80, YM2612, PSG;
  band-limited (windowed sinc) resampling to 48 kHz, optional tone filter.
- Widescreen 16:9 / 16:10 on field maps, painted from the game's own map data, and in
  battles (background mirrored at the sides); sprite culling widened (`hints.json`).
- Direct3D 11 output (flip model) with OpenGL 2.0 and GDI fallbacks; shader filters
  shared between HLSL and GLSL: sharp pixels, edge-directed pixel-art smoothing, CRT,
  curved CRT, dither blending, a picture-tube look applied only to story pictures,
  blurred side bars.
- HD packs: capture screen layers (F9 or automatically), upscale them with any tool,
  import; tiles are replaced at draw time and recoloured through palette fades.
- Clock-paced frames with dynamic audio rate control (smooth on 60–360 Hz monitors).
- Pause menu with mouse, rebinding, XInput and DirectInput gamepads, rewind,
  fast-forward, battle speed-up, autosave, save slots with pictures and dates, play
  statistics, screenshots (F12).
- Shop helper: the highlighted item compared with what each party member wears
  (`src/shop.c`, from the game's item table and shop routine).
- Technique and skill descriptions shown while the game's lists are open (battle and
  field), built from the game's own name and property tables (`src/tips.c`).
- Crash and hang reports (crash.txt, minidump, hang.txt) for bug reports.

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
FREE=1 ./build_win.sh                 # the same without game code (interpreter only)
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
