#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=gnu11 -O2 -Wall -Wextra -DLSB_FIRST -D__STDC_LIMIT_MACROS -DWANT_32BPP -DPCFX_HEADLESS \
 -I. -Imednafen -Imednafen/pcfx -Imednafen/hw_cpu -Imednafen/hw_sound -Imednafen/sound -Imednafen/video -Ipcfx-common/include -Ishell/headers \
 -o /tmp/pcfx-v23-mcu-test tests/king_v23_mcu_luma_test.c mednafen/pcfx/idct.c mednafen/pcfx/jrevdct.c -lm
/tmp/pcfx-v23-mcu-test
