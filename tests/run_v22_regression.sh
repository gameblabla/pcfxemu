#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tests/run_king_v22_huffman_stream_test.sh
tests/run_v21_regression.sh
