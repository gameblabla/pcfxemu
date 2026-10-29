# PCFXemu v13 — experimental KRAM data-retirement model

Date: 2026-10-29. Baseline: v12. This is an **opt-in experiment**, not a claim of cycle-accurate HuC6272 arbitration. No changes to TETSU's 1024-clock mixer or dot-width selection.

## Manual-supported constraints

- HuC6272 KRAM address register R0D selects the physical KRAM page via **bit 31**. The low address bits, including bit 17, are *not* the bank selector. In 1-Mbit mode the physical page is forced to zero (HuC6272 §1.3).
- The K-BUS request order, independent A/B background microprograms and display/HBlank requester distinctions are described in HuC6272 Part 2, §3.2. The manual does **not** provide a complete CPU-side posted-write drain duration, service grant phase, or all requester latencies.
- V810 instruction timing minimums do not encompass external WAIT states. The presumed one-entry KING input latch and ~16 V810-cycle back-to-back service are hardware observations, not an NEC per-access guarantee.

## v13 changes

1. Corrected the KRAM target bank in the slot scheduler, legacy contention model and profiler. The previous code mistakenly used `KRAMWA[17]`; it now uses physical page selection `KING_KRAMPage(KRAMWA[31])`.
2. Under `PCFX_KRAM_TIMING=slot`, a CPU `R0E` halfword write captures its `(page,address,value)` at acceptance. The CPU-visible R0D write address increments immediately as before, but the underlying KRAM word is changed **only when the pending write drains**.
3. A pending write has its own `KRAMWritePendingAt` deadline, distinct from `KRAMWriteBusyUntil`: the scheduler can reserve the next write before the prior data is committed. An execution update splits graphics processing at the pending retirement point so future background fetches can observe the new data.
4. Save states include pending validity, physical page, word address, value and deadline; `KING_EndFrame` rebases the deadline. An older state without these fields is treated as having eagerly committed data, preserving compatibility.
5. `PCFX_KRAM_TIMING=legacy` retains eager KRAM writes and the earlier historical measured-cost model. No automatic selection or change of the default.

## Validation performed

- Headless RGBA8888 and SDL3/offscreen builds succeed; SDL3 `--help` starts.
- KING per-bank test (amended to use bit 31 and independent bit-17 regression), slot-grant test, new pending-retirement test, TETSU/dot-width/interlace/audio tests pass.
- Original tank demo using its own emulated frame/timer counters: **103 rendered frames in 5027 timer ticks = 20.489 FPS** over a five-second measurement window, matching the v12 opt-in scheduler. Your hardware baseline is **18–19 FPS**.
- The 1024×480 still-image regression remains pixel-identical to v12 (zero changed pixels).
- A 300-frame original tank run produces exactly the same screenshot and 2 MiB guest RAM as 150 frames + save/load + 150 frames. The matched v12 300-frame screenshot is also identical in this test.

## Remaining limits (NOT implemented)

- KING's fully phase-accurate K-BUS request/grant engine for BG, RAINBOW, SCSI, ADPCM and refresh, including five HBlank refresh operations; the v12 four-grant service heuristic remains unchanged.
- Fetch visibility within *all* other devices: `KING_RunGfx` is split at pending retirement, but SCSI DMA is serviced separately and not yet chronologically interleaved with each KRAM grant. Exact rendering prefetch timing is also incomplete.
- V810 cache/pipeline and posted-I/O behavior for all devices, not only the modeled KING R0E port.
- Physical-console calibration of the write-drain latency, timing phase, and the known double-sprite regression scenario.

Do not enable this model by default merely because its tank FPS is closer to hardware; correct bus grants and sprite behavior require hardware evidence.
