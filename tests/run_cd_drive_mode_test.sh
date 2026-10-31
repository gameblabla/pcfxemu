#!/bin/sh
# Builds and runs the PC-FX CD drive model test against the headless objects.
set -eu
cd "$(dirname "$0")/.."
make -f Makefile.headless -j"$(nproc)" >/dev/null
B=build/headless-RGBA8888
OBJS=$(ls $B/*.o | grep -v pcfx_headless_cli.o | tr '\n' ' ')
cc -O1 -g -DLSB_FIRST -DPCFX_HEADLESS -DWANT_32BPP -DFRONTEND_SUPPORTS_RGBA8888 -DWANT_STEREO_SOUND -I. -Imednafen -Imednafen/pcfx -Imednafen/cdrom -Imednafen/sound \
   -std=gnu11 -o /tmp/cd-drive-mode-test tests/cd_drive_mode_test.c $OBJS -lm -lz -lpthread 2>&1 | grep -v "^$" || true
/tmp/cd-drive-mode-test
