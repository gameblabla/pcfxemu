#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
for t in tests/run_king_v13_pending_commit_test.sh tests/run_king_v12_slot_drain_test.sh \
 tests/run_king_v4_register_test.sh \
 tests/run_king_v5_register_test.sh tests/run_soundbox_v6_capture_test.sh \
 tests/run_dotwidth_test.sh tests/run_interlace_bob_test.sh; do
  "$t"
done
