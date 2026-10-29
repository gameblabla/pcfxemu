# 3Dtankdemo timing investigation — PCFXemu v7

**Status:** measured emulator behavior and reproducible diagnostic; hardware correctness **unconfirmed**.

## Input

`3Dtankdemo.zip` supplied by the user: `mytest_cd.cue` / `mytest_cd.bin`, MODE1/2048, 614400 bytes BIN. The source user expects approximately **18–19 FPS** on actual hardware; treat that as a hypothesis until measured on a physical console.

## Observations from v6

Boot with the user-supplied PC-FX BIOS, no controller input. During the 1100–2000-frame 3D scene, the in-image FPS text reads **33–34** (as rendered; no OCR used).

The 32,24,192,180 crop excludes that text and includes the tank scene. Counting every changed pixel across the crop yields ~39.2–39.6 changed video outputs per second over frames 1200–2000 at an assumed 60-field/s display. Counting only frames with **more than 250 changed pixels** gives ~35–36 substantially changing outputs/s. This is **not** proof the game completed 35–40 whole frames per second: raster writes can produce changes to an incomplete frame, and interlacing can make field data differ. Do not identify these rates with the HUD number without additional tracing.

The profiler was built with the existing `PROFILE=1` mode. Profiled 300 video frames after loading a save at emulated frame 1100:

| Counter | Value |
|---|---:|
| Instructions/field | 90,206 |
| Attributed cycles/field | 357,621 |
| CPI | 3.964 |
| `OUT.H` attributed cycles | 60.3% |
| CPU KRAM writes/field | 18,762 |
| I-cache miss rate | 1.537% |
| DRAM page penalties | 3.14% of field cycles |
| Average graphics output | 256 x 240 |

Peripherals were running at the established emulator field rate; host execution ran faster than real time in this headless test. Therefore this is **not** a host vsync or SDL3 speed problem.

### Hypotheses to test, NOT emulator facts

1. Incomplete V810 write-buffer modeling; `shell/emu/core.c` explicitly notes the store timing hack.
2. KING KRAM CPU store vs BG read arbitration and HBlank refresh slots are approximations, not exact bus schedule (see `HARDWARE_TESTS_REQUIRED.md`, KING-01).
3. Any CPU instruction timing deviation needs verification against the **V810 hardware manual**, not guessed timing slowdowns.
4. The original quoted FPS may have been measured with a different BIOS, software revision, or display mode. Compare the same demo binary.

A speculative extra +2 CPU cycles on each graphics I/O store was tested in a **temporary**, unshipped benchmark build. It did not explain the expected ~2x difference. No speculative timing adjustment has been included in v7.

## New v7 diagnostics

The headless CLI now supports:

```sh
./pcfx-headless --bios-dir /path/to/pcfx.rom --frames 2000 \
    --frame-csv /tmp/tank_frames.csv --frame-csv-rect 32,24,192,180 \
    /path/to/mytest_cd.cue
python3 tools/tank_cadence_analyze.py /tmp/tank_frames.csv \
    --from-frame 1200 --to-frame 2000 --threshold 250
```

CSV columns: frame index, current crop dimensions, changed pixels relative to the previous emulated frame, and an FNV-1a checksum over the exact cropped pixel bytes. First frame has no comparison and reports 0 changed. ROI is clipped safely to the actual display. It doesn't alter CPU, video, I/O or audio cycles; only the optional headless output conversion and checksum work increase host processing cost.

The analysis script calls these *visible-change rates*; for a genuine FPS figure, additionally trace completed-frame signals or use a hardware measurement of the intended game-frame marker. The v7 feature is intentionally opt-in. The SDL3 frontend's simulation and output are unchanged.
