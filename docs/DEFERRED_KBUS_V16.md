# PCFXemu v16 — non-CPU K-BUS transfer visibility

2026-10-29. Specification: NEC/Hudson `DOCUMENTATION/ORIGINAL_JPN/C6272_2.WRI` (HuC6272 §§3.2.2, 3.2.3, 3.3.4–3.3.5, 3.4, 3.5); HuC6230 `C6230.WRI` for ADPCM/HSYNC. Original Japanese text is normative over any translation. This note describes what the emulator does; it is NOT a hardware-calibrated bus model.

## Why this change

v15 reserved hypothetical per-bank bus slots for SCSI/RAINBOW/ADPCM but let those engines access KRAM data immediately. Thus a high-priority ADPCM request could displace a reserved SCSI request on paper while the SCSI write was already visible to all consumers. Priority bookkeeping and memory semantics disagreed. The documented request priority and SCSI byte packing justify separating acceptance, bus grant, and data visibility.

## Implemented in v16

**SCSI real DMA (REG 09/0A/0B, §3.3.4–3.3.5):** The first SCSI byte enters the low-byte latch; acceptance of the second captures a `(physical page, word address, little-endian value)` pending transaction. It reserves a per-bank grant. The KRAM word is written **at the modeled grant completion**, not at second-byte acceptance. The DMA destination address and received-byte count still advance as specified at acceptance. A queue of 128 pending words is save-stated. If a later higher-priority request displaces a scheduled SCSI grant, an identity token moves with it and the word's data visibility deadline moves as well. A queue-capacity failure reports a diagnostic and currently falls back to eager writing rather than losing bytes; this is a safety fallback, *not* verified hardware behavior. An actual SCSI WAIT/REQ backpressure implementation is still required to remove it.

**HuC6230 ADPCM transfer (§3.5):** The existing shared -HSYNC-rate transfer request now enqueues a per-channel KRAM word address and page instead of sampling data immediately. At the modeled grant completion, KING samples the KRAM word and calls `SoundBox_ADPCMReceiveWord()`. The soundbox's one-word captured-input latch and nibble-consumer remain separate. An explicit R50 channel disable cancels its pending transfer; reaching the programmed end address must not lose the last already-requested word. Grant tags also track ADPCM displacement by refresh and other priority requests. Pending channel request and completion timestamp are save-stated. `KING_GetADPCMHalfWord()` remains only as a synchronous diagnostic/compatibility accessor, not the production HSYNC path.

**KING_Update():** Graphics intervals split at the next outstanding CPU write, SCSI DMA write, or ADPCM word-read completion. No unrelated CPU slowdown was introduced. The 1024-clock video mixer, mode-3 interlace approximation, physical-dot presentation and frontend APIs are unchanged.

**Cross-client ordering regression:** At the same model time, an ADPCM word request outranks SCSI during HBlank. ADPCM consequently receives the old KRAM contents, while the later SCSI write becomes visible after its displaced grant. This is a modeled consequence of the manual's documented priority, not an independently verified waveform.

## Deliberately incomplete: HuC6271 / RAINBOW

`KING_RB_Fetch()` still reads KRAM immediately; v16 continues to reserve an approximate grant but does **not** defer compressed-data visibility. `RAINBOW_DecodeBlock()` in both accurate and fast decoder backends consumes potentially hundreds of bytes in one synchronous host callback. Making its input genuinely asynchronous requires a resumable decoder or producer/consumer FIFO driven by HuC6271 `-REQR` and granted K-BUS words; merely returning a stale or imagined byte on a failed grant corrupts the JPEG Huffman stream. That refactor has **not** been performed. Do not describe the RAINBOW path as accurate or fully deferred yet.

## Still unmeasured / assumed

* The service quantum is four V810 clocks per KING dot, not a measured electrical service width; the absolute master-clock phase is still unknown.
* The exact placement and bank pairing of the five HBlank refresh requests remain assumed.
* CPU posted-write deadlines are still booked in advance and are not retrospectively displaced by later higher-priority requests (SCSI/ADPCM are now retimed). This is an important source of potential sprite-fetch mismatch.
* SCSI DMA byte-counter and completion/IRQ semantics follow the existing receive-side control path. Exactly which completion event is tied to a **retired** KRAM word rather than the final SCSI byte is not established by the manual.
* ADPCM transfer-request/IRQ relationship to channel stop, buffer rollover, and the electrical capture strobe remains subject to hardware testing.
* An overflowing modeled SCSI pending queue presently falls back to eager writing. Hardware SCSI handshake backpressure must replace this fallback; the diagnostic is intentionally noisy.
* Other graphics read paths still fetch data in large scanline-oriented host callbacks, limiting sub-scanline data-visibility accuracy.

## Tests run

`tests/run_king_v16_scsi_deferred_test.sh`: low/high byte packing through `DoRealDMA()`, pending word visibility before and after grant, priority displacement by REF, bank isolation, 32-word burst ordering.

`tests/run_king_v16_adpcm_grant_test.sh`: HSYNC-initiated request without eager read, REF displacement, delivery through soundbox callback, R50 explicit-disable cancellation, and same-address ADPCM/SCSI ordering.

Existing v15 regression suites; headless and SDL3 builds; BIOS-based 1024x480 KING + dual VDC still; tank scene frame and guest RAM comparison. A fresh BIOS/CD boot of the user's tank demo for 1550 video frames also reached the active 3D scene with its FPS HUD (the 600-field capture was still in BIOS/startup). Same v8 saved tank state, 300 emulated fields: v15 and v16 both `102 completed frames / 5027 timer ticks = 20.290 FPS`. The earlier v15 **20.489 FPS** measurement used a different saved window; neither figure should be equated with physical PC-FX performance. Real tank result reported by user: **18–19 FPS**.

This is a substantial correctness increment for SCSI and ADPCM memory ordering, not full cycle-accurate K-BUS emulation.
