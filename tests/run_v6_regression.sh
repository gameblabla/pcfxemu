#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bash tests/run_v5_regression.sh
bash tests/run_soundbox_v6_capture_test.sh
echo "PCFXemu v6 core regression suite: PASS"
