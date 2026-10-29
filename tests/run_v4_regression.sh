#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bash tests/run_dotwidth_test.sh
bash tests/run_interlace_bob_test.sh
bash tests/run_king_v4_register_test.sh
bash tests/run_soundbox_v4_timing_test.sh
printf 'PCFXemu v4 low-level tests: PASS\n'
