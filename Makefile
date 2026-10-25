# Phantasy Star IV static recompilation
#   make            headless runner with recompiled code (build/ps4_headless_rc)
#   make interp     headless runner, interpreter only (build/ps4_headless)
#   make tests      68000 single-step test runner
CC ?= gcc
CFLAGS ?= -O2 -w
CORE = src/m68k.c src/md.c src/vdp.c src/run.c src/z80.c src/ym2612.c src/psg.c src/sound.c src/ps4_ws.c src/emu.c src/png.c src/hdpack.c src/resample.c
GEN_SRC = $(wildcard generated/ps4_recomp_*.c) generated/ps4_table.c
GEN_OBJ = $(patsubst generated/%.c,build/gen/%.o,$(GEN_SRC))

all: build/ps4_headless_rc

build/gen/%.o: generated/%.c generated/ps4_funcs.h src/rt.h src/cpu.h src/md.h
	@mkdir -p build/gen
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

build/ps4_headless_rc: $(CORE) src/headless.c $(GEN_OBJ)
	$(CC) $(CFLAGS) -DPS4_RECOMP -Isrc -o $@ $(CORE) src/headless.c $(GEN_OBJ) -lm

interp: build/ps4_headless
build/ps4_headless: $(CORE) src/headless.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) src/headless.c -lm

tests: build/sst_run
build/sst_run: tests/sst_run.c src/m68k.c src/cpu.h
	$(CC) $(CFLAGS) -o $@ tests/sst_run.c src/m68k.c

clean:
	rm -rf build
.PHONY: all interp tests clean
