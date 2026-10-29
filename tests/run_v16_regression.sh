#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tests/run_v15_regression.sh
tests/run_king_v16_scsi_deferred_test.sh
tests/run_king_v16_adpcm_grant_test.sh
