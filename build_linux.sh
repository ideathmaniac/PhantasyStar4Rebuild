#!/bin/sh
# Builds ./PS4 for Linux and the Steam Deck: the Windows front end (win/main.c)
# over SDL2 + OpenGL through port/sdl_win32.c. Needs gcc or clang, the SDL2
# runtime library (libSDL2-2.0.so.0) and libGL; the SDL2 headers are in
# third_party/. FREE=1: interpreter only, nothing from the ROM in the program.
set -e
CC=${CC:-cc}
if [ "$FREE" = 1 ]; then OUT=build/linux_free; RECOMP=""; GEN=""; else OUT=build/linux; RECOMP="-DPS4_RECOMP"; GEN="generated/ps4_table.c $(ls generated/ps4_recomp_*.c)"; fi
# NOGL=1: no desktop OpenGL, the picture goes through SDL's renderer (as on Android)
GLSRC=win/glfx.c
if [ "$NOGL" = 1 ]; then OUT=${OUT}_nogl; EXTRA_CFLAGS="$EXTRA_CFLAGS -DPS4_NO_GL"; GLSRC=""; fi
mkdir -p $OUT/obj
# the fonts, built into the program
if [ ! -f $OUT/fonts.c ] || [ third_party/fonts/DejaVuSans.ttf -nt $OUT/fonts.c ]; then
  python3 tools/embed_fonts.py $OUT/fonts.c
fi
CFLAGS="-O2 -w -fno-strict-aliasing -Iport/include -Isrc -Ithird_party/SDL2/include -Ithird_party/stb -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -D_REENTRANT $RECOMP $EXTRA_CFLAGS"
SRCS="src/png.c src/hdpack.c src/resample.c src/qol.c src/tips.c src/shop.c src/bestiary.c src/m68k.c src/md.c src/vdp.c src/run.c src/z80.c src/ym2612.c src/psg.c src/sound.c src/ps4_ws.c src/hdscale.c src/rewind.c src/emu.c win/main.c $GLSRC win/ui_soft.c port/sdl_win32.c $OUT/fonts.c $GEN"
JOBS=${JOBS:-$(nproc)}
NEWEST_H=$(ls -t port/include/*.h win/include/win32.h src/*.h win/*.h | head -1)
for o in $OUT/obj/*.o; do [ -f "$o" ] && [ "$NEWEST_H" -nt "$o" ] && rm -f $OUT/obj/*.o && break; done
echo "$SRCS" | tr ' ' '\n' | grep . | xargs -P $JOBS -I{} sh -c 'o=$0/obj/$(echo {} | tr / _ | sed s/\\.c$/.o/); [ "$o" -nt {} ] || '"$CC"' '"$CFLAGS"' -c {} -o $o' $OUT
SDL=$(ls /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 /usr/lib/libSDL2-2.0.so.0 /usr/lib64/libSDL2-2.0.so.0 2>/dev/null | head -1)
GL=""; [ "$NOGL" = 1 ] || GL=$(ls /usr/lib/x86_64-linux-gnu/libGL.so.1 /usr/lib/libGL.so.1 /usr/lib64/libGL.so.1 2>/dev/null | head -1)
$CC -o $OUT/PS4 $OUT/obj/*.o $SDL $GL -lm -lpthread -ldl -Wl,-rpath,'$ORIGIN/lib'
ls -la $OUT/PS4
