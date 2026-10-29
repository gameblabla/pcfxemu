#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -O1 -g -ffunction-sections -fdata-sections -DLSB_FIRST -DHAVE_HUC6273 -DPCFX_HEADLESS -DWANT_32BPP -DFRONTEND_SUPPORTS_RGBA8888 -D__STDC_LIMIT_MACROS -I. -Imednafen -Imednafen/pcfx -Imednafen/hw_cpu -Imednafen/hw_sound -Imednafen/sound -Imednafen/video -Ipcfx-common/include -Ishell/headers -Ishell/video/headless -std=gnu11 -Wl,--gc-sections -o /tmp/soundbox-v6-captest tests/soundbox_v6_capture_test.c -lm
/tmp/soundbox-v6-captest
