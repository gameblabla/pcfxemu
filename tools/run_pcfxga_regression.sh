#!/usr/bin/env bash
set -eu

# Portable wrapper for the regression matrix used during the C11 conversion.
# Set these paths for your local tree/images before running.
: "${EMU:=./pcfx-headless}"
: "${OUT:=./regression_out}"
: "${BIOS_STD:?set BIOS_STD to pcfx.rom or an equivalent standard PC-FX BIOS}"
: "${BIOS_GA:?set BIOS_GA to pcfxga.rom}"
: "${SAME_CUE:?set SAME_CUE to Same Game FX cue path}"
: "${NNYUU_CUE:?set NNYUU_CUE to N-nyuu cue path}"
: "${MAZE2D_DIR:?set MAZE2D_DIR to Maze2D HuEXE directory}"
: "${NNYUU_CHD:=}"

mkdir -p "$OUT/screens" "$OUT/saves"

run_one() {
  name="$1"; bios="$2"; game="$3"; frames="$4"; shift 4
  log="$OUT/${name}.log"
  ppm="$OUT/screens/${name}.ppm"
  sav="$OUT/saves/${name}"
  mkdir -p "$sav"
  echo "== $name ==" | tee "$log"
  echo "bios=$bios" | tee -a "$log"
  echo "game=$game" | tee -a "$log"
  echo "frames=$frames" | tee -a "$log"
  if [ "$#" -gt 0 ]; then echo "extra_args=$*" | tee -a "$log"; fi
  "$EMU" --bios-dir "$bios" --save-dir "$sav" "$@" --frames "$frames" --screenshot "$ppm" "$game" >>"$log" 2>&1
  status=$?
  echo "exit=$status" | tee -a "$log"
  if [ -f "$ppm" ]; then
    sha256sum "$ppm" | tee -a "$log"
    file "$ppm" | tee -a "$log" || true
  fi
  return "$status"
}

# Same Game and Maze2D reach useful visual states quickly, but N-nyuu needs a longer wait.
run_one samegame_std "$BIOS_STD" "$SAME_CUE" 1200
run_one nnyuu_cue_std "$BIOS_STD" "$NNYUU_CUE" 7200
if [ -n "$NNYUU_CHD" ]; then
  run_one nnyuu_chd_std "$BIOS_STD" "$NNYUU_CHD" 7200
fi
run_one maze2d_std "$BIOS_STD" "$MAZE2D_DIR" 1200
run_one maze2d_pcfxga "$BIOS_GA" "$MAZE2D_DIR" 1200

# The PC-FXGA BIOS CD boot path prompts for RUN.  Do not count the BIOS prompt as a pass.
run_one nnyuu_cue_pcfxga "$BIOS_GA" "$NNYUU_CUE" 7200 --auto-run
if [ -n "$NNYUU_CHD" ]; then
  run_one nnyuu_chd_pcfxga "$BIOS_GA" "$NNYUU_CHD" 7200 --auto-run
fi
