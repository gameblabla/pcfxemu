#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 3 ]; then
  echo "Usage: $0 BIOS_PATH TANK_CUE TANK_STATE_FROM_RUNNING_DEMO" >&2
  exit 2
fi
BIOS="$1"; CUE="$2"; STATE="$3"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/pcfx-headless-profile"
if [ ! -x "$BIN" ]; then echo "Build the profile executable first" >&2; exit 1; fi
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
PCFX_KBUS_PC_CSV="$TMP/pc.csv" "$BIN" --bios-dir "$BIOS" --state-in "$STATE" --frames 120 "$CUE" >"$TMP/prof.log" 2>&1
python3 - "$TMP/pc.csv" <<'PY'
import csv,sys
rows={int(r['pc_bucket'],16):r for r in csv.DictReader(open(sys.argv[1]))}
for pc in (0xe0e0, 0xe100):
 assert pc in rows, f'clear loop not observed at {pc:x}; check source ELF/layout'
 assert int(rows[pc]['writes']) > 100000, f'clear loop unexpectedly idle at {pc:x}'
 assert int(rows[pc]['contention_cycles']) > 0, 'no modeled bus contention recorded'
print('PASS: KRAM write-PC instrumentation attributes clear-loop writes and contention')
PY
