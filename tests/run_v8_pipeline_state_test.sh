#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "$#" -ne 3 ]; then
  echo 'Usage: tests/run_v8_pipeline_state_test.sh BIOS_PATH TANK_CUE INITIAL_TANK_STATE' >&2
  exit 2
fi
bios="$1"; cue="$2"; initial="$3"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
./pcfx-headless --bios-dir "$bios" --state-in "$initial" --frames 101 \
  --screenshot "$tmp/continuous.png" "$cue" >"$tmp/continuous.log" 2>&1
./pcfx-headless --bios-dir "$bios" --state-in "$initial" --frames 100 \
  --state-out "$tmp/resume.state" "$cue" >"$tmp/saving.log" 2>&1
./pcfx-headless --bios-dir "$bios" --state-in "$tmp/resume.state" --frames 1 \
  --screenshot "$tmp/resumed.png" "$cue" >"$tmp/resumed.log" 2>&1
cmp "$tmp/continuous.png" "$tmp/resumed.png"
echo 'PASS v8: V810 state-resume rendering matches continuous playback at the tank scene.'
