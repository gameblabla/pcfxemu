#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tests/run_v16_regression.sh
tests/run_king_v17_rainbow_ahead_test.sh
