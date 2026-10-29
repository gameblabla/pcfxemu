#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I. \
  -o /tmp/pcfx_v27_yuv_matrix_test tests/king_v27_yuv_matrix_test.c -lm
/tmp/pcfx_v27_yuv_matrix_test
