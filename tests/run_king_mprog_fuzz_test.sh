#!/bin/sh
# Usage: tests/run_king_mprog_fuzz_test.sh [iterations] [seed]
# Built with ASan+UBSan so stray reads/writes in the BG fetch code show up as hard failures.
set -eu
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/pcfx-king-mprog-fuzz
# Sanitizer runtimes are not installed everywhere (libasan/libubsan missing here): try them, fall back to a plain build.
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
echo 'int main(void){return 0;}' > "$OUT.probe.c"
cc $SAN -o "$OUT.probe" "$OUT.probe.c" 2>/dev/null || { echo "(no sanitizer runtime available: plain build)"; SAN=""; }
cc -O1 -g $SAN -ffunction-sections -fdata-sections \
   -DLSB_FIRST -DHAVE_HUC6273 -DPCFX_HEADLESS -DWANT_32BPP -DFRONTEND_SUPPORTS_RGBA8888 -D__STDC_LIMIT_MACROS \
   -I. -Imednafen -Imednafen/pcfx -Imednafen/hw_cpu -Imednafen/hw_sound -Imednafen/sound -Imednafen/video \
   -Ipcfx-common/include -Ishell/headers -Ishell/video/headless -std=gnu11 -Wl,--gc-sections \
   -o "$OUT" tests/king_mprog_fuzz_test.c -lm
"$OUT" "$@"
