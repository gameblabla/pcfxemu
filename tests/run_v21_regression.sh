#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
for suite in \
  tests/run_king_v21_jpeg_stream_test.sh \
  tests/run_king_v20_rainbow_incremental_test.sh \
  tests/run_king_v17_rainbow_ahead_test.sh \
  tests/run_king_v18_scsi_backpressure_test.sh \
  tests/run_king_v16_adpcm_grant_test.sh \
  tests/run_king_v15_bus_priority_test.sh; do
  "$suite"
done
