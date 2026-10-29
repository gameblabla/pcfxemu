#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
for test in \
 tests/run_king_v19_rainbow_async_test.sh \
 tests/run_king_v17_rainbow_ahead_test.sh \
 tests/run_king_v18_scsi_backpressure_test.sh \
 tests/run_king_v16_adpcm_grant_test.sh \
 tests/run_king_v15_bus_priority_test.sh; do
 "$test"
done
