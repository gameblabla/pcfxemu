#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I. tests/rainbow_concealment_test.c -o /tmp/pcfx_rainbow_concealment_test
PCFX_RAINBOW_CONCEALMENT=0 /tmp/pcfx_rainbow_concealment_test 0
PCFX_RAINBOW_CONCEALMENT=1 /tmp/pcfx_rainbow_concealment_test 1
