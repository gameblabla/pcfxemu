#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=gnu11 -Wall -Wextra -Werror -O2 -I. -o /tmp/pcfx_v24_yuv_color_test tests/king_v24_yuv_color_test.c -lm
/tmp/pcfx_v24_yuv_color_test
