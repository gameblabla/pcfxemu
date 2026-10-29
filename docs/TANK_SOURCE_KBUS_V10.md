# PCFXemu v10 — exact tank source cross-check and per-PC KRAM cost attribution

## Reproducible provenance

- The user's `src.zip` includes `PCFX/src/main.c`, `PCFX/src/pcfx.c`, `PCFX/src/fastking.s`, `mytest.elf`, and `mytest_cd.bin`.
- `src.zip`'s `PCFX/mytest_cd.bin` is **byte-identical** to the earlier `3Dtankdemo.zip`'s `mytest_cd.bin`: SHA-256 `8542fa66ee68d2e219d3240c576e0658b5de8cef48e923c8a28106d72d05652f`.
- `KingClear()` writes exactly `3840 * 8 = 30720` KRAM halfwords every completed render frame. It selects KING R0E and outputs 8 halfwords to `0x604` per iteration. The compiled, inlined main loop at V810 PC `0xE0F0..0xE10C` confirms eight consecutive `OUT.H` stores followed by `add -1; bne`.
- `getFps()` counts increments of `nframe` against `zda_timer_count/1000`. The timer period is `1423`, and `nframe` advances after `vsync(0)` in the main loop. The counter measures completed rendering loops, not display changes or host FPS.

## Controlled emulator experiment (NOT real-console measurement)

The original source was rebuilt twice with only one call suppressed in `main()`: `KingClear()` (draw-only) and `draw_3d_object_quads(tank...)` (clear-only). Independent CD builds were run in PCFXemu v9/v10 with 300-field profiled windows after 1100 warm-up video periods. For the game-defined FPS we compared the actual `_nframe` and `_zda_timer_count` values in guest RAM at 1400 and 1700 total video periods (5.0 seconds), using symbol addresses from each variant's ELF.

| Build | `nframe` delta | timer-tick delta | Measured in-game FPS | KRAM halfword writes per video period |
|---|---:|---:|---:|---:|
| Exact original | 169 | 5027 | 33.62 | 18849.46 |
| Clear-only | 280 | 5026 | 55.71 | 28633.50 |
| Draw-only | 300 | 5027 | 59.68 (VSync limited) | 2641.05 |

The original game on hardware is **18–19 FPS according to the user**; independent physical measurement is not available here. The original emulator's red on-screen counter displayed 33–34, consistent with `_nframe`/timer measurement.

**Specific hot PC attribution** for the baseline 300-field run, enabled by the v10 `V810_PROFILE` build:

| 32-byte PC bucket | KRAM writes during sample | Wait cycles charged | Contention cycles charged | Source |
|---|---:|---:|---:|---|
| `0x00E0E0` | 2,599,396 | 6,480,990 | 13,020,701 | clear loop |
| `0x00E100` | 2,599,393 | 7,765,579 | 13,013,739 | clear loop |
| `0x00E860` | 456,049 | 0 | 2,278,520 | textured model spans and other non-clear writes |

The 5,198,789 clear-loop KRAM writes constitute **91.9%** of all 5,654,838 KRAM port writes in the baseline interval. The emulator charges about **134,270 cycles per video period** of latch + contention costs to clear-loop writes alone. These are modeled charges, not physical KING bus cycles.

## What can be inferred — and what cannot

1. The tank is a sustained KRAM halfword-write throughput test, not merely a 3D arithmetic benchmark. A fixed wait per `OUT.H` is nevertheless **not** a documented hardware rule.
2. If all of the real-hardware FPS deficit relative to 33.62 FPS were due to these ~33,000 writes per completed frame, the necessary extra delay would be on the order of **15–17 V810 clocks per KRAM halfword**. This is a *diagnostic budget*, not a calibrated wait-state or permission to hardcode it.
3. `draw-only` is VSync-limited; it does not establish the underlying uncapped 3D-renderer throughput. The exact impact of KRAM contention cannot be derived without physical clear-only and draw-only tests.
4. The emulator's existing per-bank throughput curve is empirical. NEC/Hudson HuC6272 documentation defines priority and the bank-A/B microprogram slots, but **does not provide the physical V810 posted-store queue depth or precise K-BUS service edge schedule**.
5. The v10 change adds *profiler-only* PC attribution. No default speed, V810 pipeline rule, KING timing, or 1024-clock video behavior is changed.

## New profiler output

Build `make -f Makefile.headless PROFILE=1 BUILD_DIR=build/headless-prof-v10 PRGNAME=pcfx-headless-profile`.

Set `PCFX_KBUS_PC_CSV=/tmp/kbus_pc.csv` to export the 32-byte writer-PC histogram. The report also shows the 16 highest-cost buckets, with KRAM write count, posted-latch wait cycles, and contention cycles separately. Use `v810-objdump -d mytest.elf` to match hot PCs to the actual guest instructions.

The independent `tools/tank_guest_fps.py` reads two guest RAM snapshots created with `--dump ram`, resolves `_nframe` and `_zda_timer_count` using `v810-nm`, and calculates the game's own FPS without OCR.

## Hardware-validation requirements

- **PERF-05:** Run the original, clear-only, and draw-only ROMs from this source on the same stock PC-FX at the same KRAM microprogram/bank settings. Record each game's own FPS counter and visible VSync rate. Compare against the three emulator runs above.
- **PERF-06:** Capture a timer-controlled microbenchmark issuing a fixed number of `OUT.H` writes on bank A and B, with four wide BG0 fetch slots on the competing bank and on the opposite bank. Sweep through active display vs HBlank. Histogram CPU completion times and KING BUSY.
- **PERF-07:** Compare paired `OUT.H` sequences with an intervening independent ALU operation, `IN.H` fencing, and branch/data dependency. Determine when the CPU can overlap a posted write with work and when an outstanding write must drain.

These entries belong in `docs/HARDWARE_TESTS_REQUIRED.md`. They are required before replacing the empirical KRAM timing model or claiming 18–19 FPS convergence is accurate.
