#!/bin/sh
# DIAG9 header-path (K13), interrupt-state (K14) and PLAY regression in pcfx-headless.  See tests/rainbow_header_path_test.py.
set -eu
cd "$(dirname "$0")/.."
make -f Makefile.headless -j"$(nproc)" >/dev/null
exec python3 tests/rainbow_header_path_test.py "$@"
