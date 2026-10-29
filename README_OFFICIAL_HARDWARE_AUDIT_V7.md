# PCFXemu v7 — Tank demo timing diagnostics and official-hardware follow-up

Base: PCFXemu v6. **No speculative CPU, KING bus, or video clock adjustments.**

The user provided a 3D tank demo believed to achieve **18–19 frames/s on stock PC-FX**. With the supplied PC-FX BIOS the demo boots under v6/v7 and shows **33–34** in its own frame-rate overlay. The apparent image changes ~39 times/s in a full steady section, but output changes are not synonymous with completed game frames. The discrepancy deserves a real hardware test; simply changing emulator speed would conceal an unproven cause.

## Code added

- `headless/pcfx_headless_cli.c`: `--frame-csv PATH` and `--frame-csv-rect X,Y,W,H`. Per-emulated-frame region checksum, changed-pixel count, displayed-region dimensions. Optional and post-emulation only; no CPU cycle, audio, video or SDL timing changes.
- `tools/tank_cadence_analyze.py`: analysis of visible frame changes with threshold and interval options.
- `tests/run_tank_cadence_test.sh`: repeatable test requiring user-supplied BIOS and tank demo CUE/BIN.
- `docs/TANK_DEMO_TIMING_INVESTIGATION.md`: profiling figures, limitations and proposed hardware tests.
- `docs/HARDWARE_TESTS_REQUIRED.md`: PERF-01 and PERF-02, including concrete KRAM bus and V810 write-buffer probes.

## Validated

- v6 test suite (HuC6261/TETSU, KING registers, dot width, interlace, HuC6230): PASS.
- Headless RGBA8888 CLI builds; SDL3 offscreen build and link succeed.
- `--frame-csv` test: 1700 frames, 1700 rows, 192×180 region, nonstatic gameplay.
- v6 versus v7 demo output at 2000 emulated frames: pixel-identical 256×240 screenshots.
- 300-field V810 profile after frame 1100: ~90,206 instructions/field, ~357,621 cycles/field, 60.3% attributed to `OUT.H`, ~18,762 CPU KRAM writes/field.

This release adds **diagnostic accuracy and a reproducible regression**, not a claim of a resolved FPS discrepancy. Official manuals govern specified behavior; missing bus timings must not be guessed. See `docs/HARDWARE_TESTS_REQUIRED.md`.
