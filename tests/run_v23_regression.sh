#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tests/run_king_v23_mcu_luma_test.sh
tests/run_v22_regression.sh
