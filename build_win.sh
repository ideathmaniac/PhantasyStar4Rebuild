#!/bin/sh
# Builds PS4.exe for 64-bit Windows with clang + lld-link, no Windows SDK:
# C library calls go to msvcrt.dll, Win32 to the system DLLs, through import
# libraries made here from win/def/*.def.
set -e
# FREE=1: the build without the game's code (no generated/, interpreter
# only); it contains nothing from the ROM and can be shared.
if [ "$FREE" = 1 ]; then OUT=build/win_free; RECOMP=""; GEN=""; else OUT=build/win; RECOMP="-DPS4_RECOMP"; GEN="generated/ps4_table.c $(ls generated/ps4_recomp_*.c)"; fi
mkdir -p $OUT/obj $OUT/lib
for d in win/def/*.def; do
  llvm-dlltool -m i386:x86-64 -d $d -l $OUT/lib/$(basename $d .def).lib
done
CFLAGS="--target=x86_64-pc-windows-msvc -O2 -w -ffreestanding -nostdlibinc -Iwin/include -Isrc -mno-stack-arg-probe -fno-stack-protector $RECOMP"
SRCS="src/png.c src/hdpack.c src/resample.c src/qol.c src/tips.c src/shop.c src/bestiary.c src/m68k.c src/md.c src/vdp.c src/run.c src/z80.c src/ym2612.c src/psg.c src/sound.c src/ps4_ws.c src/hdscale.c src/rewind.c src/emu.c win/main.c win/diag.c win/glfx.c win/d3dfx.c win/ui_soft.c win/setjmp_x64.c $GEN"
JOBS=${JOBS:-2}
# a changed header rebuilds everything (the per-file check below only looks at the .c)
NEWEST_H=$(ls -t win/include/*.h src/*.h win/*.h | head -1)
for o in $OUT/obj/*.obj; do [ -f "$o" ] && [ "$NEWEST_H" -nt "$o" ] && rm -f $OUT/obj/*.obj && break; done
echo "$SRCS" | tr ' ' '\n' | grep . | xargs -P $JOBS -I{} sh -c 'o=$0/obj/$(echo {} | tr / _ | sed s/\\.c$/.obj/); [ "$o" -nt {} ] || clang '"$CFLAGS"' -c {} -o $o' $OUT
llvm-rc -I win /fo $OUT/obj/res.res win/ps4.rc 2>/dev/null || true
RES=""; [ -f $OUT/obj/res.res ] && RES=$OUT/obj/res.res
lld-link /nologo /entry:Start /subsystem:windows /nodefaultlib /stack:8388608,8388608 /out:$OUT/PS4.exe /map:$OUT/PS4.map \
  $OUT/obj/*.obj $RES $OUT/lib/*.lib
ls -la $OUT/PS4.exe
