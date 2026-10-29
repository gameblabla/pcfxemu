#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if (( $# < 2 )); then
    echo "Usage: $0 BIOS_PATH TANK_CUE [FRAMES]" >&2
    exit 2
fi
bios="$1"
cue="$2"
frames="${3:-1700}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
./pcfx-headless --bios-dir "$bios" --save-dir "$tmp/saves" \
    --frames "$frames" --frame-csv "$tmp/frames.csv" \
    --frame-csv-rect 32,24,192,180 "$cue" >"$tmp/run.log" 2>&1
python3 - "$tmp/frames.csv" "$frames" <<'PY'
import csv, sys
rows=list(csv.DictReader(open(sys.argv[1])))
assert len(rows)==int(sys.argv[2]), (len(rows),sys.argv[2])
assert all(int(r['region_width'])==192 and int(r['region_height'])==180 for r in rows)
assert len(set(r['fnv64'] for r in rows))>5, 'video appears static'
print('PASS: cadence CSV contains all frames and changing video')
PY
python3 tools/tank_cadence_analyze.py "$tmp/frames.csv" --from-frame 1100
