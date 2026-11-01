#!/bin/sh
# KRAM access-width regression (ST.H/ST.W/OUT.W/...): runs the DIAG9 labs K10-K12 in pcfx-headless and checks them.
# Needs the v810 toolchain, the RAINBOW player sources (RAINBOW_PLAYER_DIR) and a BIOS.  See tests/king_width_lab_test.py.
set -eu
cd "$(dirname "$0")/.."
make -f Makefile.headless -j"$(nproc)" >/dev/null
exec python3 tests/king_width_lab_test.py "$@"
