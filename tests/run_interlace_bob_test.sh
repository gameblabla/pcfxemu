#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -I. \
 tests/interlace_bob_test.c sdl3/pcfx_interlace.c -o /tmp/pcfxemu-interlace-bob-test
/tmp/pcfxemu-interlace-bob-test
rm -f /tmp/pcfxemu-interlace-bob-test
