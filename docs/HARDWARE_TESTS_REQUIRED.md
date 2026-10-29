# PC-FX / PC-FXGA hardware measurements required

Revision: PCFXemu v6, 2026-10-29. **This file is a hardware-validation queue, not a list of proven software defects.**

## Evidence policy

- **Ground truth for specified behaviour:** NEC/Hudson's HuC6261 (`C6261.WRI`, November 10, 1995), HuC6272 (`C6272_1.WRI`, `C6272_2.WRI`, `C6272_3.WRI`) and HuC6230 (`C6230.WRI`) supplied in the PC-FX toolkit. The included English markdown versions are translations; where they disagree, consult the original Japanese files and mark the issue ambiguous.
- **Hardware observation:** use to calibrate unspecified electrical timing, analog response, undefined/forbidden modes, or known errata. A test revealing conflict with a manual should be documented as a possible silicon revision/erratum, not quietly used to override the spec.
- **Simulator-only results** (PNG screenshots, source-image MSE, reconstructed 1024-clock samples) cannot establish the physical bandwidth or a particular perceived display resolution.
- **Separate platforms:** stock PC-FX uses HuC6270 x2, HuC6271, HuC6272 and HuC6261; HuC6273 is PC-FXGA-specific.

Each entry gives the relevant document, why emulation remains unproven, and a suggested **falsifiable** experiment. Existing software fixes must not be described as verified silicon behaviour.

## Video and mixer — prioritize for the 480i project

| ID | Scope / documentation | Unknown or approximation | Suggested hardware check |
|---|---|---|---|
| VID-01 | HuC6261 §§2.6.3, 2.7, 2.10 | **Exact mode-3 half-dot time offset**, its polarity and width/edge masking in **both** dot-clock modes. v3+ resamples finished RGB lines; it does not reproduce analog sample-and-hold. | Test odd/even fields with a thin white vertical line against black, shift mode 2→3, preserve the separate fields, and record Y timing with sub-dot precision. Repeat in 5 and 7 MHz modes. Determine offset, direction, boundary blanking and whether it affects all sources. |
| VID-02 | HuC6261 §§2.7, 2.10; VDC horizontal timing | Absolute KING vs dual-VDC phase (default 18 TETSU ticks is historical, not validated). | Generate VDC white steps and KING white steps on alternating raster rows. Trigger scope on HSYNC and extract the rising edge relative to sync, at both clocks. Sweep source start and scroll settings. |
| VID-03 | HuC6261 §§2.3, 2.7 | Whether each theoretical **one-21-MHz-tick** mixed-layer pulse survives DACs, how it shifts in mixed transparency cases, and the exact priority decision phase. | Render independent VDC/KING 3:4 grids and a 1-tick pulse test; capture the Y channel with sufficient bandwidth. This is **not** a claim of a 1024-pixel framebuffer. |
| VID-04 | HuC6261 §§2.8, 2.11 | End-to-end Y/U/V DAC impulse response, overshoot, chroma transition widths, and retail S-Video/optional component modification transfer functions. v3+ analog presets are illustrative only. | Feed flat levels, 1-, 2- and 3-clock pulses and 1..7 MHz luma gratings; record Y and UV separately before/after output modification. Avoid comb filters and deinterlacers for measurement. |
| VID-05 | HuC6261 §§2.6.1–2.6.2, 2.11 | Precise half-line field sync waveforms, equalization/serration, vertical timing on external sync/re-lock, 0–5 raster oscillator startup. Headless/SDL3 deliver pixel images, not analog NTSC sync. | Record odd/even field VSYNC/HSYNC with logic analyzer and analog output during reset, mode transitions, and external sync if available on the test board. |
| VID-06 | HuC6261 §2.1.3 (CPA/CPW/CPR) | Manual warns active-display palette access causes visible noise but gives no noise waveform/which dots are affected. No fake random noise is implemented. | CPU read/write one palette entry at several clock phases during active display; compare physical video and unaffected/off-display control. Test double-halfword word access as documented. |
| VID-07 | HuC6261 §2.1.3 / §2.11 | Read data from undocumented/write-only TETSU registers, invalid register numbers, and electrical undriven bus value. v4 uses `0xFFFF` sentinel; **not a verified bus value**. | Place a documented known value on the bus and read the write-only port with timing variations; report whether returned bits are last-bus, pull-ups, or register-specific. Never drive invalid bits in normal software. |
| VID-08 | HuC6261 §§2.3, 2.5 | Undocumented illegal identical priority values, all-plane-disabled + back-cellophane special case, and special blending at dot-clock boundary. | A/B opaque/translucent test strips under otherwise legal settings, then separately illegal priority ties. Record Y/U/V. Distinguish spec-mandated from undefined effects. |

## KING / RAINBOW / KRAM

| ID | Scope / documentation | Unknown or approximation | Suggested hardware check |
|---|---|---|---|
| KING-01 | HuC6272_2 §3.2 | Exact arbitration slots between BG, RAINBOW, SCSI and CPU, and **five HBlank refreshes**. v4 only adjusts aggregate CPU stalls; no exact arbitration schedule exists. | Write a CPU KRAM-read/write timing benchmark sweeping start tick, microprogram BG access slots and competing RAINBOW/DMA loads. Histogram BUSY durations throughout active/HBlank; distinguish refresh slots. |
| KING-02 | HuC6272_2 §3.6 | Mid-scanline BG control and microprogram-dependent fetch: v4 redraws visible remainder, but reads current KRAM contents as a whole line. | Alter BG mode and KRAM tiles at different x positions in one raster, capture transition edge and any prefetched pixels. Include priority-zero BG and 1M/4M KRAM mode. |
| KING-03 | HuC6272_1 REG 41/43; HuC6272_2 §3.4.2 | v5 defers transfer source address and block count to the end of the 16H block. Exact race window at the boundary and whether unrelated R42/R40 are also delayed are not electrically specified; translation prose is broader than register-table I*. | Change R41/R43 at 15H, 16H and at different CPU phases around block completion. Vary R42 during transfer and verify split playback. Compare destination RAINBOW output and interrupt raster. |
| KING-04 | HuC6272_2 §3.4.2(4) | Three trailing guard words, prefetching across block boundaries, and the precise HuC6271 REQR stop/next-block handling. | Use valid and malformed compressed block streams with/without the 3 zero dummy words; compare first 1–2 lines of next block. Test both decoder backends independently. |
| KING-05 | HuC6272_2 §3.3 | SCSI RESET IRQ assertion and release timing, phase mismatch, pseudo-DMA control bit side effects, raw subchannel read cadence. Original source explicitly guesses some of these. | SCSI probe ROM with timestamped IRQ counters, bus status reads, subchannel interrupts, RST and PHASE transitions during DMA. Compare across real CD-ROM media. |
| KING-06 | HuC6272_2 §3.3.4; `docs/king-dma-erratum.md` | Count-retirement and trailing handshake anomalies are experimentally hypothesized fork behavior, not the manual's normative operation. | Exercise exactly matching/short/long DMA byte counts, status-phase transitions, and repeated R0B read/ack; record countdown, busy and IRQ. Keep explicit compatibility flags isolated. |
| KING-07 | HuC6272_2 §§3.6.3–3.6.7 | Undefined invalid BG dimensions, illegal microprograms, and the CPU-visible read pipeline timing. No manual-guaranteed output for prohibited configurations. | Only if a real game needs it: run constrained patterns with illegal sizes and microprogram combinations, preserving a known-good reference. Do not extrapolate to legal modes. |

## HuC6230 ADPCM / audio

| ID | Scope / documentation | Unknown or approximation | Suggested hardware check |
|---|---|---|---|
| AUD-01 | HuC6272_1 REG 50 / HuC6272_2 §3.5.4 | v5+ latches a rising READ EN on the next -HSYNC and clears READ EN immediately. v6 adds a separate HSYNC-captured input-word latch. First transfer vs first audible nibble latency still needs a source-synchronized FIFO model. | Trigger on -HSYNC and log HuC6230 CS/RH/-L with a one-shot R50 enable issued just before/after HSYNC. Count first transfer and first DAC output, including both channels; compare with the v6 shared-phase approximation. |
| AUD-02 | HuC6230 §4.2 | Transfer cycle is **shared between channels**; only the final two bytes per channel before cycle boundary are valid. v6 now implements an HSYNC-synchronized shared-cycle 16-bit input latch per channel, but does not simulate separate byte strobes or the exact KING→HuC6230 bus handshakes. | Stream alternating A/B words at different offsets; test reset of both channels versus one, overrun, and the moment transfer is captured. Compare DAC output/IRQ timings. |
| AUD-03 | HuC6230 §§2.2.2, 2.3 | Electronic PCM volume register envelope is v4's documented-period **approximation**; exact analog VCA curve, near-zero steps, and R10 reset/rate edge latency remain unmeasured. | Capture step response at high, mid and lowest volumes; measure delay for R10 written on both sides of falling HSYNC. Rate switching while reset released is prohibited by the manual. |
| AUD-04 | HuC6272_2 §3.5 | ADPCM KRAM fetch contention with BG/RAINBOW/SCSI and hold-state recovery under interrupted transfers. | Generate KRAM contention with fixed nibble signatures and record exact audio discontinuities, channel status and KRAM BUSY latency. |

## Optional PC-FXGA only

| ID | Documentation | Unknown or approximation | Hardware probe |
|---|---|---|---|
| GA-01 | HuC6273 chapters 1–3 | Shader rounding, 12-bit I/C reconstruction, facet/Gouraud material lighting and interpolation. Existing code contains appearance-based heuristics. | Reference color ramp and 3D test mesh on **PC-FXGA**, not stock PC-FX, captured before RGB postprocessing. |
| GA-02 | HuC6273 Appendix A/B | PE clear write overtaking swap; dummy frame-control ordering; PVLD hidden clear allowed only at 256×240. | Command FIFO ordering probes and per-pixel hidden-clear tests at 256×240 and 320×200 on PC-FXGA. |

## Recommended measurement discipline

1. Capture hardware revision, firmware, sync source, output connection, oscilloscope/capture frequency response, test program hash, and exact register values.
2. Keep raw separate-field samples and sync waveform where possible; disable TV deinterlacing/comb filters when characterizing the silicon.
3. Provide **observed vs specification vs emulator** per probe. Characterize illegal modes as silicon behavior, not mandatory conformance.
4. Use the same signal-generation homebrew with 5/7 MHz VDC, KING-only, mixed-clock overlays, interlace mode 2 and half-dot mode 3.
5. Only promote an uncertain timing constant into an emulator default after reproducible hardware measurements; otherwise retain the raw digital output and make the experiment optional.

## Tank demo frame-rate discrepancy (added v7)

| ID | Scope | Emulator observation | Required physical-console check |
|---|---|---|---|
| PERF-01 | User-supplied `3Dtankdemo.zip`, stock PC-FX | V6 headless screenshot counter shows 33–34; 1200–2000 video frames show ~39.4 changed screen images/s (~35–36 changes/s above a 250-pixel crop threshold). These are **not interchangeable FPS measures**. | Run this exact CUE/BIN on a known stock PC-FX with original video timings. Record 10+ seconds of video, the displayed FPS digits, 16-bit hash of ROM, capture field rate, and intervals between COMPLETE tank frames. User reports an expected 18–19 FPS; not verified here. |
| PERF-02 | V810 CPU + KING KRAM write buffering; see `shell/emu/core.c` FIXME | In a 300-field steady-state V810 profile, `OUT.H` consumes 60.3% of attributed cycles and ~18,762 KING CPU KRAM writes/field occur. The modeled CPU write buffer and BG read/CPU write bus arbitration may be underspecified. | Run a KRAM bandwidth probe (16-bit burst stores to R0E and R0D) from RAM with and without BG0 background fetch, sampling V810 timer and external -HSYNC. Use both in-display and HBlank windows; establish actual write-buffer depth and KRAM wait cycles before changing the emulator default. |

Do **not** patch rendering time or multiply wait cycles merely to match the 18–19 FPS hypothesis. The video clock should stay at the documented rate. A fixed experimental +2-cycle per graphics I/O store did not reproduce 18–19 FPS; it left steady-state image-change counts roughly in the same range. This experiment was not committed as a feature.

## V8 update: tank bank-A timing isolate (PERF-03)

`docs/TANK_PIPELINE_KBUS_V8.md` documents the new regression evidence. The tank demo's 5,628,577 modeled KRAM writes during 300 emulated video periods **all target bank A**; its four wide-color active fetch slots also belong exclusively to bank A. Exact slot order, posted write overlapping/retirement, and HBlank REF service remain uncalibrated. Compare that specific bank-A load against a bank-B-only variant of the same homebrew. The architecture manual's instruction cycle table is explicitly a *no-wait/no-hazard minimum* and does not establish an external write-buffer depth. Neither the 18–19 FPS target nor any fixed additional wait is treated as normative.

## V9 update — posted-write / bank-local arbitration (PERF-04)

**User-confirmed reference:** the supplied `3Dtankdemo.zip` runs at **18–19 FPS** on a physical PC-FX; this report is our target, not independently captured by this emulator audit. PCFXemu v9 still displays approximately **33–34** in the game's own FPS counter. See `TANK_KBUS_V9.md` for the measurement method and detailed modelled stall breakdown.

The official HuC6272 document specifies independent BG microprograms per A/B bank, BG priority only during display, and REF/ADPCM/RAINBOW/SCSI priority during horizontal blanking. V9 corrects the bank-selection and BG/HBlank classification of the existing throughput fit. Its nominal 5-clock KRAM read is still a measured-midpoint **approximation**, not a specified device latency. The manuals do not identify the exact write-buffer depth, service clock phases or amount of CPU/KRAM overlap.

**Targeted real-hardware experiments:** (1) issue KRAM CPU burst writes to A vs B with the BG's 4 busy slots only on A, then only on B; timestamp before/after with the CPU timer, (2) sweep the same write burst across 8 microprogram phase positions, including A/B slot adjacency variations, (3) sweep across HBlank and count the five refresh penalties independently of ADPCM and RAINBOW, (4) compare `OUT.H; OUT.H`, `OUT.H; ADD; OUT.H`, `OUT.H; IN.H` using I-cache-resident code and an externally visible completion fence, (5) record the tank game's *internal completed-frame counter* and physical red FPS HUD with the same binary/version.

**Do not** force KRAM waits to make the image counter equal 18 or 19. First reconcile the actual CPU pipeline, posted write retirement and bank arbitration with the hardware traces. A measured correct model must also reproduce the existing 0-, 1-, 2-, 4-, 8-, and 16-busy-slot KRAM throughput probes, not just the tank demo.

## V10 — tank source cross-check and required confirmation

See `TANK_SOURCE_KBUS_V10.md` for exact source and ELF disassembly, profiler histograms, and three emulator-build measurements. `src.zip`'s binary hashes exactly match `3Dtankdemo.zip`.

- **PERF-05:** Measure original / clear-only / draw-only demo FPS on **the same stock PC-FX**, keeping VDC, KING, KRAM bank and timer settings identical. Emulator measurements: 33.62 / 55.71 / 59.68 FPS, respectively; original console's original demo: **18–19 FPS as reported by user**. The other two console figures are unknown.
- **PERF-06:** Determine actual CPU KRAM write throughput and variance for bank A/B, BG0 wide-color microprogram (4 fetches), during display/HBlank, and with SCSI/ADPCM load. Profile at different HSYNC phases, not only average FPS.
- **PERF-07:** Isolate V810 posted-write queue depth, instruction overlap, and KING BUSY handling by interleaving OUT.H/ALU/IN.H under identical conditions. A large computed budget of missing clocks is **not a hardware constant**.

## V11: absolute microprogram phase and CPU posted-write retirement (PERF-08)

The v11 profiler now records **relative** per-bank microprogram slot occupancy when each CPU KRAM port write is issued (see `TANK_KBUS_PHASE_V11.md`). For the tank's bank-A-only load, half of the ~4.22M active-display writes occur on model non-NOP slots 0–3; half on NOP slots 4–7. Do **not** interpret these as grant-phase or retirement-phase distributions; the V810 write-posting behavior, KING grant phase and each request's service latency remain unknown.

**Hardware test to resolve:** run a microbenchmark with eight otherwise identical 30,720-halfword `OUT.H` bursts. For each, rotate one/two/four non-NOP BG microprogram slots through the 8-dot group while keeping background contents, bank selection, and video period unchanged. Compare timer counts for bank A, bank B, and alternating banks. Repeat with `OUT.H; ALU; OUT.H`, `OUT.H; IN.H` fence and with writes issued just before HBlank. Capture -BUSY / -HSYNC if available. Publish *measured clock cycles per retired write*, not just a final FPS. This is required before enabling a cycle-grant scheduler as the default.

**Specification versus unknown:** HuC6272 §3.6.7 fixes slot *order*, bank independence and NOP behavior. §§3.2.2–3.2.3 fix priority and refresh count. They do not specify the PC-FX motherboard's exact cross-device phase, CPU posted-write queue depth, or request-retirement latency.

## V12: posted latch grants and double-sprite guard (PERF-09)

The v12 `PCFX_KRAM_TIMING=slot` mode is **experimental** and **OFF by default**. From the exact same tank state, it produces a 20.489 FPS guest-loop rate compared with 33.817 FPS using legacy average contention, and the user independently reports 18–19 FPS on real PC-FX. These FPS figures are not proof of hardware accuracy.

A hardware observer reports (a) one KING input latch, (b) CPU WAIT on premature next write, (c) about 16 V810 clocks / four KING dot cycles per write, (d) additional write/read/refresh BUSY delays, and (e) **double sprites if KING is overslowed**. A physical-console probe should verify: a two-write sequence with programmable ALU gaps; KRAM read-after-write visibility; both banks with BG fetch slots swept over each of eight phases; HBlank with five refresh grants; full BG occupancy; and a sprite-sensitive game that fails when KING is artificially over-slowed. Record register state, CPU timer counts, peripheral readback, and final video fields. The current model does not defer RAM data commits and leaves non-BG grant schedules incomplete. Keep it opt-in until those observations are reproduced.

## v13 follow-up: single-entry posted write (2026-10-29)

**Highest priority:** Capture back-to-back KING R0E halfword writes with 0, 1, 2, 4, 8, 16, 32 and 64 V810 cycles of ALU work between writes. Log total CPU cycles, the first visible KRAM value, and the instruction in which WAIT occurs. Repeat for low versus high word address bit 17, and for page 0 versus 1 with KRAM MOD=1. The page should be selected by R0D bit 31, not bit 17.

**Bus-grant matrix:** Repeat with no BG clients, BG slots 0–3 and 4–7 on each physical bank, and while RAINBOW, ADPCM, SCSI and refresh operate. Capture timing around active/HBlank transition. Compare against `PCFX_KRAM_TIMING=slot` predictions without adjusting constants solely to hit one demo's FPS.

**Visibility races:** Arrange a CPU write to a background tile or table during video fetch; sample whether the current, next or later line observes it. Repeat with SCSI DMA/ADPCM/Rainbow reading the same address. This checks whether actual data visibility is tied to input-latch drain or another internal buffer/fetch point.

**Known regression:** Run the game previously reported to display duplicated sprites when KING waits are too long. Verify source-independent sprite timing before considering the new model for default emulation.

**Unresolved external CPU behavior:** V810 instruction-cache misses, posted writes to other devices, and pipeline stalls. A cycle-accurate KRAM scheduler alone cannot establish CPU accuracy.

## V14 default slot model: regression gates (2026-10-29)

The experimental v12/v13 KRAM latch/retirement model is now **the v14 default** (legacy selectable with `--kram-timing legacy` or `PCFX_KRAM_TIMING=legacy`). Older statements above that refer to an opt-in model describe historical v12/v13 policy and are superseded. This change reflects a chosen structural model and the strong tank FPS improvement, **not** a new physical K-BUS calibration.

**V14-CPU-01:** Verify NEC V810 cache activation timing after `LDSR CHCW` with a cache-thrashing loop, cache dump/restore, and entry-limited ICC invalidations. Count cycles with and without instruction cache and external WAIT; normal instruction timing table 5-11 contains cache-hit/hazard-free minima only. Check pipeline store/branch flag hazards separately.

**V14-KBUS-01:** Measure a repeated `OUT.H` burst with 0–64 cycles of ALU spacing under eight rotating BG fetch-slot layouts; monitor CPU WAIT and first visible write. Repeat bank A/B and 1-Mbit/4-Mbit configurations, and across active/HBlank boundaries. This checks the model's assumed four-grant interval and absolute microprogram phase.

**V14-KBUS-02:** During HBlank, vary ADPCM, RAINBOW, SCSI, DMA and refresh activity; check whether all five documented REF accesses block CPU writes in the expected order. Do not arbitrarily assign these to five hard-coded slots in advance of measurement.

**V14-VIDEO-01:** Run the previously reported double-sprite game with slot and legacy modes, capturing the same scene and checking sprite timing. Preserve a fallback until this is reproducible.

**V14-PERF-01:** On physical PC-FX compare source-matched original tank, clear-only and draw-only builds. The reported original is 18–19 FPS; the emulator default currently reports 20.489 FPS. The remaining difference must be resolved by bus/CPU measurements, not adjusted using an arbitrary wait multiplier.

## v15: source-specific K-BUS grant tests (2026-10-29)

The v15 scheduler is the only supported model; historical selectable legacy
instructions in earlier sections above are superseded. The HuC6272 manual
establishes the *requester priority and refresh count*, **not** absolute
phase or latency. Verify the following on a retail PC-FX:

- **KBUS15-01:** HBlank `REF` grant positions and whether five refreshes
  reserve both banks together. Sweep an I/O write burst across HSYNC.
- **KBUS15-02:** SCSI DMA paired bytes to KRAM word commit timing; measure
  a CPU write/read into the same bank while `DMAStatus.EN` is active.
- **KBUS15-03:** RAINBOW `-REQR` compressed block transfer pacing vs actual
  KRAM word reads and their arbitration against BG and CPU.
- **KBUS15-04:** ADPCM word-request timing relative to -HSYNC and sampling
  control, under concurrent SCSI DMA and RAINBOW transfer.
- **KBUS15-05:** Read-after-write and DMA/CPU visibility races, including
  whether previously booked CPU retirements can be displaced.
- **KBUS15-06:** The known doubled-sprite game under the single v15 model,
  plus the physical tank clear-only and draw-only variants.

The approximate four-clock request quantum, HBlank refresh placement and
source event timing remain model choices until those tests are completed.

**KBUS15-07 (emulator determinism):** A 150+save+150 frame tank run in v15
produced the same visible frame as an uninterrupted 300 frame run, but 18 bytes
at approximately 0x1FFD48–0x1FFF70 in guest RAM/stack differed. Compare the
V810 CPU state, KING reservation table and event timestamps immediately
before and after state reload. Determine if this is harmless scratch-stack
state or a transient pending K-BUS retirement mismatch. Until resolved, avoid
claiming full byte-identical save-state determinism.

## v16 — deferred SCSI / HuC6230 word capture / RAINBOW producer timing

The SCSI and ADPCM data paths now capture payloads at K-BUS *model* grant completion. The following are deliberately **not** claimed as verified from the manuals alone:

* **KBUS16-01 — DMA word/IRQ ordering.** Transfer two SCSI bytes to a known KRAM word under BG/REF pressure. Time KRAM visibility, destination-address increment, counter decrement, DMA END and -IRQ independently. In particular, does END occur with final byte reception or final KRAM grant?
* **KBUS16-02 — DMA backpressure.** Saturate the SCSI→KRAM path while BG requests every slot. Measure whether SCSI REQ/ACK is held while the word input latch is busy; v16 currently has a 128-entry **model queue**, not a physical one-entry FIFO. The overflow fallback must not be mistaken for silicon behavior.
* **KBUS16-03 — ADPCM return timing.** Modify a known ADPCM KRAM word between the falling HSYNC request and its actual K-BUS grant, and record the decoded nibble. Sweep refresh, BG, and same-bank SCSI contention. Verify what occurs when R50 disables the channel or the final word ends playback.
* **KBUS16-04 — RAINBOW -REQR.** Measure the HuC6271 compressed-word FIFO depth, transfer requests, grant timing, and precise 16-line-block prefetch boundary; this is required to implement a resumable decoder with deferred KRAM reads.
* **KBUS16-05 — sprite regression.** Re-run the game with the reportedly double-rendered sprite under overslow KING timing and capture each field while varying KRAM occupancy. This remains an unresolved validation target.
* **KBUS16-06 — CPU grant displacement.** Interleave posted OUT.H stores with later higher-priority requests and confirm whether an already booked CPU write can be postponed. The v16 CPU path has **not** yet been restructured to support displacement.

## v17 — HuC6271 RAINBOW post-block prefetch and Makendou Z

- **RAINBOW17-01 (read-ahead):** Hudson Soft `C6272_2.WRI` §3.4.2(4) requires three dummy words per 16-line block and explains that KING prefetches beyond the `-REQR` stop. Oscilloscope- or probe-measure the actual prefetch count, KRAM-address phase, and whether the words are captured while `-REQR` is still active. The v17 three-word snapshot is a *partial software model*, not a confirmed FIFO timing implementation.
- **RAINBOW17-02 (producer/decoder):** Measure bus grants and decoder underflow under CPU BG/SCSI/ADPCM contention. The existing codec's eager input must eventually be replaced by a resumable producer/consumer pipeline; its timing cannot be deduced solely from the official throughput description.
- **SCSI17-03 (critical):** The Makendou Z CD test repeatedly exhausts the emulator's 128-entry pending SCSI DMA queue; the current emergency eager-write fallback violates KRAM visibility semantics. Determine real REQ/ACK backpressure and queue/latch depth, then replace the fallback. Do not use an arbitrary delay merely to hide the diagnostic.
- **SAVE17-04:** Preserve read-ahead and its K-BUS tagged deadlines across save/load, including states from v16 which lacked these fields. Verify suspended HuC6271 input state once a resumable decoder is implemented.

## v18 — HuC6272 SCSI backpressure; RAINBOW still synchronous (2026-10-29)

- **SCSI18-01, REQ/ACK stall onset:** Use a programmable SCSI target or bus logic analyzer with KRAM bank A BG microprogram slots varied from 0 to 8 occupied. During a large SCSI DMA, measure which byte's REQ is held and how long ACK remains low. v18 stalls at the *second* byte if its emulator reservation queue cannot accept another word; that is protocol-compatible but not proof of the physical latch depth.
- **SCSI18-02, DMA count granularity:** During the two bytes of a SCSI word, read HuC6272 `0x0A` through the CPU at controlled phases. The original manual specifies a decrement for each received byte. Confirm whether the CPU-visible register reflects the intermediate odd count and exactly when the DMA END flag/IRQ occurs relative to the second ACK and final KRAM grant.
- **SCSI18-03, reservation depth and grant displacement:** Saturate SCSI with competing BG/RAINBOW/ADPCM/refresh traffic, detect whether transfers can remain pending after the engine's DMA END signal, and compare with v18's 128-entry **software** reservation array. The array is not a claimed physical FIFO.
- **RAINBOW18-04, decoder underflow:** Exercise a JPEG/RLE block with deliberate SCSI/CPU write contention against the compressed stream and probe HuC6271 `-REQR`, its ready/stop signal, the three post-block guard words, and actual 16H display latency. The v18 decoder still consumes most words synchronously; the diagnostics only detect one class of premature read-ahead consumption.
- **VIDEO18-05, double sprites:** Continue the known game-specific double-sprite regression test on a physical PC-FX before concluding that the unified K-BUS scheduler is calibrated. Do not adjust generic wait states merely to match the tank demo's FPS.

## v19: HuC6271 asynchronous compressed input

**RBO-19A (transfer rate):** HuC6272 §3.4.2 specifies the 16H transfer and three-word read-ahead but not the complete KRAM grant phase, exact -REQR timing or FIFO depth. Capture the actual -REQR assertion and KRAM bus grants using a valid sequence of JPEG/RLE blocks, while separately enabling BG, SCSI and ADPCM. Compare the number of grants and phase of each with v19's *software-selected* four-clock grant quantum.

**RBO-19B (starvation):** Force a compressed-data block to miss its normal fetch window. Measure whether HuC6271 continues displaying the previous block, displays a transparent/blank block, or behaves differently. v19 currently delays the buffer swap while an entire compressed block is being staged; this behavior is a **model assumption**, not an observed silicon fact.

**RBO-19C (boundary/guard):** Put different sentinel words before and after the mandated 3-word zero guard, mutate those addresses during SCSI DMA, and observe whether the next 16H block consumes the original prefetched value or a later KRAM update. Compare with v19's grant-time captured-byte semantics.

**RBO-19D (decoder progression):** Measure header, Huffman code, RLE run and quantizer progress when the input FIFO temporarily empties. v19 does not yet suspend/resume entropy decoding within a block, and its buffering is much larger than any documented hardware FIFO. Do not treat image identity in Makendou Z as timing equivalence.

## v20 additional RAINBOW measurements

1. **RLE token underflow:** run a stream with an expansion token split across successive KRAM grants. Observe whether the chip pauses at token boundaries, byte boundaries or pixel expansion boundaries. The emulator pauses at token boundaries as an unverified conservative choice.
2. **-REQR/FIFO response:** deliberately starve compressed input while BG and SCSI compete for the same KRAM bank. Determine physical -REQR assertion/deassertion phase, FIFO depth and low-water mark.
3. **Two-byte dummy / three-word lookahead:** test token boundaries that enter the two inner dummy bytes, while guarding against reads into the three zero words. The programmed transfer and memory layout should match Hudson's manual.
4. **JPEG partial progress:** provoke a stall between a Huffman code prefix and its remaining bits, and observe whether DC predictors and output columns retain their prior committed state.
5. **16H buffer swap/starvation:** measure which visible line changes if the next block has not completed by the documented transfer boundary, and how a resumed decode is presented.

## v21 — JPEG entropy-input and Huffman progression

- **RBO21-01, FF stuffing across grants:** Place an entropy `0xFF` at the end of one received 16-bit word, delay the next word with competing BG/ADPCM accesses, and probe when the chip consumes the two-byte stuffed pair. v21's atomic software step is a conservative model, not a measured internal timing rule.
- **RBO21-02, FF quantizer headers:** Place `0xFF` values in the 128-byte FF quantization table, then delay the first entropy bytes. Confirm whether quantization state becomes effective progressively or atomically at the next 16H decode boundary.
- **RBO21-03, DC/AC partial symbols:** Starve the real chip midway through DC and AC Huffman symbols. Observe REQR behavior, whether the current 16-line output is held, and the first column affected after data resumes. v21 does not yet implement actual Huffman pause/resume.
- **RBO21-04, FIFO capacity:** Measure REQR high/low-water thresholds and the maximum unread data physically buffered. The emulator's 128 KiB staging array is not an estimate of the silicon's FIFO size.

## v22 addition — JPEG coefficient availability versus actual output

- Measure RAINBOW `-REQR`/`-HSYNC` signals under deliberately delayed KRAM grants while decoding an F8 and FF JPEG block. Establish when completed DC/AC symbols become visible relative to the 16H image output.
- Verify real Huffman decoder behavior when a value/amplitude straddles a 16-bit KRAM grant; no bytes should be fabricated. Check FF stuffing and the documented two inner dummy bytes plus three guard words.
- Determine actual input buffering/watermark behavior and whether RAINBOW stalls, retains an old column, generates confetti, or drops output when an MCU misses its deadline. Do not assume the v22 software staging allocation is physical capacity.
- Verify whether quantizer rescaling codes can alter the active MCU within a partially completed column, and how changes persist across FF/F8 blocks.
- Test save/restart from an incomplete component and compare real console versus precise digital capture. The v22 parallel coefficient parser is not yet wired into actual video output.

## v23 RAINBOW incremental-IDCT work

- **Software investigation first (not hardware dependent):** Four of 6,240 JPEG strips in the observed Makendou test do not yet match the established pixel decoder. Determine whether special null-column/control-block semantics, quantizer timing or another parser error accounts for them. Do not enable replacement rendering until resolved.
- **Hardware-dependent:** HuC6271 IDCT/MCU output publication timing relative to K-BUS grants and the 16H display schedule, especially with intentionally starved compressed input. Hudson Soft's public device notes do not provide a fully specified cycle-level stage pipeline.
- **Hardware-dependent:** Actual compressed-input FIFO capacity and priority/grant phase; do not select a fixed size or add artificial stalls based solely on Makendou performance.

## V24-COLOR: mandatory analog output and color-space validation (2026-10-29)

The v23 `king.c:RebuildUVLUT()` applied JPEG full-range YCbCr inverse coefficients to all final HuC6261 YUV, including palette-based graphics. v24 replaces these with conventional analog YUV reconstruction. HuC6261 §2.8 establishes `Y=0..255`, `U=V=0x80` neutral and 8-bit linear analog D/A conversion; **it does not specify the complete real-console video-encoder RGB matrix, gamma, chroma bandwidth or board analog gain**.

**Physical-console test required:** write a still-image ROM cycling direct KING YUV888 pixels, RAINBOW YUV video, and HuC6261 palette YUV844 colors for identical numerical YUV triplets. Use neutral steps at Y=0,16,64,128,192,235,255, and U/V sweeps about 128, with saturated red/blue/green and chroma-only patterns. Capture S-Video Y/C or analog YUV component mod output with calibrated level measurements and a known decoder. Do not treat TV screenshots or HDMI capture automatic RGB expansion as ground truth. Report black/white levels, hue/chroma amplitude, channel clipping, gamma, and temporal behavior. Identify whether the washed-out observation is present on actual hardware, HDMI upscaler only, SDL3 window only, or lossless emulator PNG as those are different faults.

**Software-only remaining work:** validate RAINBOW incremental IDCT chroma U/V against the established decoder (v23 compares Y only); inspect chroma quantization-table and subsampling edge cases; resolve the four exceptional JPEG strips before switching pixel production to the new backend. Also verify SDL3 texture color-space/ICC/HDR presentation on desktop rather than only offscreen. Do not increase saturation globally to mimic a preferred TV look without physical calibration.

## v25: Color regression commit and analog calibration

- Determine retail PC-FX Y/U/V DAC transfer gains and component encoder behavior using calibrated physical measurements. The HuC6261 manual defines ranges and center but does **not** provide a complete sRGB matrix.
- Test KING YUV888 direct output and YUV844 palette output separately before applying any RGB color corrections.
- Test HuC6271 reconstruction in YUV before TETSU, including the U/V planes and chroma interpolation across 16-line boundaries; do not infer correctness from luma matching.
- Reproduce any damaged/confetti strips with original compressed data and bus-grant logs instead of treating noise detection and previous-frame replacement as a valid hardware function.
