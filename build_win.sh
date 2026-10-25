#!/bin/sh
# Builds PS4.exe for 64-bit Windows with clang + lld-link, no Windows SDK:
# C library calls go to msvcrt.dll, Win32 to the system DLLs, through import
# libraries made here from win/def/*.def.
set -e
OUT=build/win
mkdir -p $OUT/obj $OUT/lib
for d in win/def/*.def; do
  llvm-dlltool -m i386:x86-64 -d $d -l $OUT/lib/$(basename $d .def).lib
done
CFLAGS="--target=x86_64-pc-windows-msvc -O2 -w -ffreestanding -nostdlibinc -Iwin/include -Isrc -mno-stack-arg-probe -fno-stack-protector -DPS4_RECOMP"
SRCS="src/m68k.c src/md.c src/vdp.c src/run.c src/z80.c src/ym2612.c src/psg.c src/sound.c src/ps4_ws.c src/hdscale.c src/rewind.c src/emu.c win/main.c generated/ps4_table.c $(ls generated/ps4_recomp_*.c)"
JOBS=${JOBS:-2}
echo "$SRCS" | tr ' ' '\n' | grep . | xargs -P $JOBS -I{} sh -c 'o=$0/obj/$(echo {} | tr / _ | sed s/\\.c$/.obj/); [ "$o" -nt {} ] || clang '"$CFLAGS"' -c {} -o $o' $OUT
llvm-rc -I win /fo $OUT/obj/res.res win/ps4.rc 2>/dev/null || true
RES=""; [ -f $OUT/obj/res.res ] && RES=$OUT/obj/res.res
lld-link /nologo /entry:Start /subsystem:windows /nodefaultlib /stack:8388608,8388608 /out:$OUT/PS4.exe \
  $OUT/obj/*.obj $RES $OUT/lib/*.lib
ls -la $OUT/PS4.exe
