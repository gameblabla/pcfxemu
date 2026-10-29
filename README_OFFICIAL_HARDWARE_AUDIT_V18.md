# PCFXemu v18 — SCSI DMA backpressure and RAINBOW readiness diagnostics

This revision fixes the known v17 SCSI DMA pending-word queue-exhaustion fallback. Hudson Soft.s HuC6272 bus priority model and CPU deferred write scheduling are unchanged; there is no legacy timing mode. See `docs/SCSI_BACKPRESSURE_V18.md` for the documented behavior, tests and remaining hardware assumptions.

**Change:** SCSI DMA no longer acknowledges a second byte without first reserving its pending KRAM halfword. The old eager-write fallback is removed. The DMA count now decreases on each accepted byte, per Hudson §3.3.4. `PCFX_SCSI_BACKPRESSURE_STATS=1` logs retry attempts; `PCFX_RAINBOW_STATS=1` logs RAINBOW read-ahead and premature-consumption diagnostics.

**Verification:** Headless/SDL3 (offscreen) builds and KING/TETSU/audio unit tests pass. Makendou Z cold boots for 1600 frames and produces **33 modeled SCSI backpressure retries** with no immediate data writes. Its 1024×480 screenshot matches v17. The photographic homebrew (2400 frames) and tank (1500 frames) screenshots are also unchanged. No physical hardware was tested.

**Not completed:** Fully deferred HuC6271 compressed-data FIFO, resumable JPEG/RLE decode, physical bus-grant phase, exact SCSI latch capacity, complete multi-client save-state determinism, V810 cache calibration, real analog video timing and the reported doubled-sprite regression. See `docs/HARDWARE_TESTS_REQUIRED.md`.
