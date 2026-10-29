# PCFXemu v12 — experimental KING write-latch drain timing

Date: 2026-10-29. Baseline: v11. This release **does not change the default timing model**.

## New evidence and interpretation

A PC-FX hardware observer reports that a CPU write to KING enters a single input latch; a subsequent write while the latch is still draining holds the V810 in WAIT. Roughly **16 V810 cycles per KRAM halfword**, or **four 5.37-MHz KING clock periods**, were observed in one scenario. Other observations mention additional internal BUSY-after-write / BUSY-after-read / refresh delays and warn that excessive KING stalls can introduce double sprites in a game. These statements are valuable observations, not complete formal timing tables.

The NEC/Hudson HuC6272 Part 2 documentation establishes **two independently programmed eight-slot KRAM background microprograms**, request priority during display and HBlank, and five refresh operations during HBlank. It does *not* establish V810-to-KING phase, write-latch acceptance-to-retirement delay, exact RAM grant lengths, or refresh edge timing. NEC's V810 instruction timing minimums similarly exclude external waits.

## What v11 already did, and the problem

v11 charges a roughly 16-cycle posted-write drain with `KRAMWriteBusyUntil`. It also adds a fractional *per-write* empirically fitted BG contention cost **directly to the CPU timestamp** after every write. That average cost cannot overlap ALU work between writes and is not tied to the phase of the eight-slot microprogram. KRAM data becomes visible immediately on the write's issue path, before modeled latch retirement.

## v12 opt-in experiment

Set `PCFX_KRAM_TIMING=slot` before launching either frontend. With this option:

1. An `OUT.H` to KING's KRAM data register is accepted into the existing modeled **one-entry pending latch**, provided the prior write's drain has completed. Otherwise the CPU WAITs until the next acceptance.
2. Four 5.37-MHz KING-dot grant opportunities are required to drain that latch. A programmed *visible* BG fetch on the **target KRAM bank** consumes an opportunity. Opposite-bank fetches cannot block the target bank. In HBlank, BG fetches no longer consume grant opportunities.
3. All that time is charged **to the pending latch deadline**, not as a per-write CPU penalty. Non-KRAM CPU instructions can overlap the drain. When the next write arrives, it waits only for the outstanding deadline.
4. The legacy average BG-contention and flat per-write curves are **not applied on top**, avoiding double charging.
5. Full active-display occupancy can defer this *model's* CPU grant until the end of the active period; **refresh/ADPCM/SCSI/Rainbow service is not fully represented**.

Implementation: `mednafen/pcfx/king.c`, `KING_KRAMSlotDrainDeadline`, `KING_KRAMWriteWaitCycles`, `KING_KRAMWriteContentionCycles`. The shared emulation core ensures headless and SDL3 use the same timing. The `PCFX_KRAM_TIMING` environment value is sampled once per process. There is no save-state format change; existing `KRAMWriteBusyUntil` is reused. Do not change timing modes mid-session or mix save states from different timing models for game-accuracy comparisons.

**Crucial remaining limitation:** v12's new timing model defers the *deadline* only, not the *commit of KRAM data*. `KING_Write16` still writes RAM immediately. That means BG fetch / DMA / CPU read observability and 100%-accurate sprite timing have not been solved. Any match to one game's FPS could be coincidental; DO NOT make `slot` the default without verifying data visibility and end-to-end bus arbitration.

## Actual measured guest FPS — identical 300-video-frame windows

Input: supplied `3Dtankdemo.zip`, BIOS `pcfx.rom`, initial `tank1100.state`. The game's own `_nframe` and `_zda_timer_count` RAM counters are read from 300 and 600 additional video-frame captures and scored by `nframe_delta*1000/timer_delta` (not wall-clock or changed-picture rates).

| Timing | Completed rendered frames | Timer ticks | Guest FPS | HUD after 300 video frames |
|---|---:|---:|---:|---:|
| Default v11-equivalent | 170 | 5027 | **33.817** | 34 |
| `PCFX_KRAM_TIMING=slot` | 103 | 5027 | **20.489** | 20 |
| Stock PC-FX | — | — | **18–19** | User's hardware observation |

The experimental model is near but **not at** the hardware target, and its four-grant service rule is not independently verified. Do not tune its parameters to force 18–19 FPS.

## Regression verification

- Headless C build and SDL3 offscreen build: passed (SDL3 linked against the existing bundled `libSDL3.so.0`; desktop window backend not validated here).
- v4/v5/v9 KING, v4/v6 HuC6230, horizontal width, bob/weave, and v12 slot-scheduler unit tests: passed.
- Slot scheduler test: no BG contention = 16 CPU cycles; four occupied A slots = 32-cycle drain at selected phase; B independent; blanking releases BG slots; full-occupancy case is bounded at HBlank.
- 480i photographic sample: exact 1024x480 byte/pixel identity to the prior v11 reference and to the new default mode from the same save state.
- Original tank: default v12 image identical to v11; opt-in changes the game timing and image appropriately.
- Save state: 300 uninterrupted frames in slot mode compared with 150 frames, save/load, and 150 more: video image and 2MB of guest RAM exactly match.
- SDL3 offscreen started with slot setting; graphics GUI correctness and actual physical video have not been measured.

## Next engineering steps

1. Model actual single-entry posted KRAM data with `pending bank/address/value` and commit it on retirement **before** dependent reads and background fetches. Include queue state and in-flight absolute timestamps in save states.
2. Make source-side CPU/IO write visibility and bus grant logic aware of R0D/R0E address autoincrement and R0E reads; add a memory fence regression.
3. Integrate request priorities for active/HBlank and the five refresh operations, and document all still-inferred service lengths.
4. Run paired KRAM bandwidth microbenchmarks against the historical 0/1/2/4/8/16-occupied-slot hardware measurements, including BG on A versus B.
5. Independently test the game in which excess KING stalls cause **double sprites**. This is an additional fidelity gate and could disprove the simple model even if the tank FPS is close.
