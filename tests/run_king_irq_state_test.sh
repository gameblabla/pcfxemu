#!/bin/sh
# K14-after-K13 interrupt/timer state from the DIAG9 hardware photo.  See tests/king_irq_state_test.py.
set -eu
cd "$(dirname "$0")/.."
make -f Makefile.headless -j"$(nproc)" >/dev/null
exec python3 tests/king_irq_state_test.py "$@"
