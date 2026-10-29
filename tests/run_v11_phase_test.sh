#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "$#" -ne 3 ]; then
  echo "Usage: tests/run_v11_phase_test.sh BIOS_DIR TANK_CUE INITIAL_TANK_STATE" >&2
  exit 2
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
python3 tests/validate_v11_timing.py
PCFX_KBUS_PHASE_CSV="$tmp/phase.csv" ./pcfx-headless-profile \
  --bios-dir "$1" --state-in "$3" --frames 120 "$2" >"$tmp/log" 2>&1
python3 tests/validate_v11_timing.py "$tmp/phase.csv"
