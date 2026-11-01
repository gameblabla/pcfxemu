#!/bin/sh
# DIAG11 tank speed-test regression (rows A..L pinned to the console's photos).  Needs the v810 toolchain, the RAINBOW player sources and a BIOS.
set -eu
cd "$(dirname "$0")/.."
make -f Makefile.headless -j"$(nproc)" >/dev/null
exec python3 tests/tank_speed_lab_test.py "$@"
