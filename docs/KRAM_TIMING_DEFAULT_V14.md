# PCFXemu v14 — default KING KRAM timing and source-level validation

Date: 2026-10-29. Baseline: PCFXemu v13. Scope: both headless and SDL3 frontends.

## Default policy

`PCFX_KRAM_TIMING` **unset** now selects the v12/v13 per-bank, one-entry posted KRAM write latch with deferred memory visibility. `PCFX_KRAM_TIMING=slot` explicitly selects the same model. `PCFX_KRAM_TIMING=legacy` selects the pre-v12 empirical busy-slot average and eager write visibility. Equivalent launch options are `--kram-timing slot|legacy` in both frontends; an explicit CLI option overrides the inherited environment. The model is selected once per process; it must not be changed after starting emulation.

This change is **not** certification of cycle-accurate timing. It is a deliberately chosen, testable default because it better represents the documented independent KRAM banks and eight-step background access microprogram, as well as the reported single pending input latch. Legacy remains available for regression and games that exhibit sprite/graphics timing problems.

## What is actually documented

**HuC6272 Part 2, §3.2.2–3.2.3:** priority during visible display is BG > RAINBOW > SCSI > CPU. HBlank has REF > ADPCM > RAINBOW > SCSI > CPU, including five refresh operations per HBlank. The arbiter reconsiders pending requests after servicing a higher-priority request.

**HuC6272 Part 2, §3.6.7:** each KRAM bank has an independent eight-dot microprogram, with simultaneous A/B access. Disabled BG priorities stop that plane's microprogram; NOP slots are unused by BG.

**HuC6272 Part 1, §1.3 and CPU KRAM R0D/R0E:** address register bit 31 selects the requested physical page (forced to page 0 in 1-Mbit mode); CPU data writes target KING's KRAM interface.

**NEC V810 Architecture Manual, chapter 5, Table 5-11:** listed instruction execution cycles are minima on a cache hit, without hazards or external WAIT. Parenthesized `OUT`/`ST` cycles concern consecutive execution of the same instruction. They cannot be used as an external KRAM-write service-time constant.

## Model assumptions that are not in the manuals

The model uses four available KING-dot grants per CPU KRAM halfword and the CPU-side one-entry posted latch, with a rough 16-V810-cycle empty-bus inter-write interval. This assumption is based on external hardware-observer reports, not a documented HuC6272 bus-grant latency. The current implementation does not grant KRAM to all competing devices (including all five HBlank refresh operations) at their precisely measured instants. The origin phase relative to the V810 and absolute arrival-to-retirement latency remain unverified. The V810 instruction cache's external refill/WAIT costs are also only approximations. The model must therefore be checked for more than just correct FPS.

## Measured emulator results

These are headless emulator results using the same user-provided tank ELF and save state, measuring `_nframe` / `_zda_timer_count` in guest RAM between 300 and 600 emulated display periods (5,027 timer ticks):

| Launch mode | Completed rendering loops | Calculated guest FPS |
|---|---:|---:|
| No timing flag/environment | 103 | 20.489 |
| `PCFX_KRAM_TIMING=slot` | 103 | 20.489 |
| `PCFX_KRAM_TIMING=legacy` | 170 | 33.817 |

The user's independently supplied physical PC-FX result is approximately 18–19 FPS. **Neither frame-rate proximity nor absence of a graphics artifact proves the bus timing is accurate.** The supplied tank game is unusually write-heavy: about 30,720 16-bit KRAM writes per completed frame just for framebuffer clearing.

Additional tests passed: default/slot/legacy selector policy, existing v13 KING/TETSU/audio/dot-width/interlace unit suites, 1024x480 still-image screenshot matching between default and explicit slot, and 150-frame save/load/150-frame versus 300-frame continuous guest-RAM identity under default slot timing. Headless and SDL3 builds passed. SDL3 linked to the console/offscreen SDL3 3.4.18 build and was smoke-tested with `--help`, not as a real desktop-window or real-console capture.

## Usage

```
./pcfx-headless --kram-timing slot --frames 600 game.cue
./pcfx-headless --kram-timing legacy --frames 600 game.cue
./pcfx-sdl3-ui --kram-timing legacy game.cue
```

Alternatively set `PCFX_KRAM_TIMING=legacy` before launching. Do not compare a save-state across different timing models as proof of deterministic emulation; use a common prior state and then run each model separately.

## Still missing

A phase-accurate K-BUS request/grant scheduler including REF, ADPCM, RAINBOW, SCSI and DMA; real KRAM write-read visibility tests; exact V810 posted-store/IO overlap and cache refill cycles; and a specific regression test for the reported double-sprite game. See `HARDWARE_TESTS_REQUIRED.md`.
