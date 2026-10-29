# v10: tank source / V810 KRAM writer attribution

**Hardware accuracy policy:** official NEC/Hudson documentation defines specified behavior. Unknown timings are not substituted with arbitrary emulator slowdowns.

This incremental release adds a profiler-only V810-origin histogram for KING KRAM writes. In `PROFILE=1` builds, KING's write-wait and per-bank contention hooks now record the executing V810 PC at 32-byte granularity and accumulate write count, posted-latch wait cycles and empirical contention cycles. The two quantities remain distinct. Set `PCFX_KBUS_PC_CSV=/path/out.csv` to export the full histogram. Normal release builds compile this out entirely.

The user supplied complete source for the tank demonstration. The ELF and CD were matched to the previously tested ROM and two controlled diagnostic discs were rebuilt: clear-only and draw-only. See `docs/TANK_SOURCE_KBUS_V10.md` for the source, timing measurements, limitations, and a hardware experiment list; the independent `tools/tank_guest_fps.py` computes completed-frame FPS directly from RAM dumps and guest ELF symbols.

**Emulator results:** original 33.62 FPS, clear-only 55.71 FPS, draw-only 59.68 FPS (VSync limited). User's real-hardware original demo reports 18–19 FPS. These are different provenances; no measurement of the diagnostic variants exists on hardware yet.

The v10 profiler changes **do not modify** V810 instruction timing, KING bus service delays, VSync, HuC6261/TETSU composition, or SDL3 behavior. The default 1024-clock mixed-rate renderer and 480i output remain unchanged. The observed discrepancy is not fixed and must not be obscured by changing timing constants without bus/CPU evidence.

Validation: v9/v10 headless source-demo screenshots identical for a resumed 400-field state; v10 headless release/profile and SDL3 offscreen backend build; existing v6 and v9 KING tests pass; dedicated source-PC attribution passes.
