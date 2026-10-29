#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=gnu11 -O2 -Wall -Wextra -o /tmp/pcfx-v22-huffman-test tests/king_v22_huffman_stream_test.c
/tmp/pcfx-v22-huffman-test
