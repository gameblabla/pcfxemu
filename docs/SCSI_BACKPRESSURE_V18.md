# v18: HuC6272 SCSI DMA backpressure and byte-count correctness

**Baseline:** v17. **Primary specification:** Hudson Soft `DOCUMENTATION/ORIGINAL_JPN/C6272_2.WRI`, §3.3.4 (translated counterpart: `DOCUMENTATION/ENGLISH_TRANSLATION/C6272_2/C6272_2.md`). In the SCSI DATA IN phase, the HuC6272 accepts two bytes, writes one 16-bit KRAM word, increments the destination word address, and decrements its DMA byte counter **for each received byte**. The SCSI REQ/ACK handshake permits the initiator to postpone acceptance of a byte.

## Implemented

- `DoRealDMA()` now reports whether the current SCSI byte was accepted. For a second byte, it **first reserves the deferred KRAM word write**; if the model queue or K-BUS reservation is full, it returns false without changing `DMALatch`, the halfword flip-flop, destination address, DMA byte count, KRAM, or DMA completion status.
- The SCSI receive pump asserts ACK only after successful acceptance. If no KRAM request can be scheduled, ACK stays deasserted and REQ is held by the target. The pump retries the **same** byte at a later modeled DMA event.
- **Removed** the unsafe v16/v17 fallback that wrote directly to `DMAPagePtr` when the pending queue filled. No DMA write bypasses the K-BUS retirement time.
- The DMA byte counter decreases once per accepted SCSI byte, consistent with §3.3.4. An unaccepted second byte leaves the counter unchanged. DMA completion still occurs only on the final completed word.
- `PCFX_SCSI_BACKPRESSURE_STATS=1` logs the number of rejected second-byte attempts at shutdown (a **retry count**, not the number of different bytes or a physical measurement).
- Existing RAINBOW read-ahead remains unchanged; `PCFX_RAINBOW_STATS=1` also reports attempts to consume a matching post-block word **before** its grant. This is only a diagnostic, not an asynchronous JPEG decoder.

## Validation performed

- New `tests/run_king_v18_scsi_backpressure_test.sh` covers per-byte DMA count, full-queue refusal, unchanged byte/word state on refusal, retry after capacity returns, deferred KRAM visibility, and completion on final second byte.
- The complete v17 unit test suite passes, including KING priority, RAINBOW read-ahead, ADPCM capture, save-state-sensitive write retirement, interlace and dot-width modes.
- `Makeruna! Makendou Z (Japan).chd`, using the user's BIOS: 1,600 frames with auto-RUN. **33 rejected second-byte attempts** and no old pending-queue-exhaustion fallback messages. The 1024×480 frame is pixel-identical to v17's 1,600-frame reference.
- Same 2,400-frame photographic PC-FX homebrew: **1024×480, pixel-identical** to v17; same 1,500-frame tank test: **256×240, pixel-identical** to v17. These comparisons validate regressions, not exact real-hardware performance.
- Headless and SDL3 executables built with CHD; SDL3 `--help` tested against an offscreen SDL3 3.4.18 library. No desktop-window validation or physical-console comparison.

## Why this is a correctness improvement, but not a complete physical SCSI model

The HuC6272 specification supplies handshake sequencing and the two-byte-to-one-word DMA transfer; it does **not** specify a 128-word pending queue or exact K-BUS grant duration. 128 entries are an **emulator reservation structure**, not claimed hardware storage. Backpressure is applied at the second byte of a modeled word only when that structure cannot schedule a write. On real hardware the handshake may stall sooner (e.g., a one-word input latch). Likewise, the exact relationship among DMA END, the final SCSI ACK, and final KRAM commit still requires a hardware timing test.

The emulator still computes graphics and pumps SCSI in large event slices. It is not yet a wholly cycle-accurate multi-client simulator. Such a refactor must preserve monotonic event ordering and avoid issuing reservations at an already-executed timestamp.

## Still incomplete: HuC6271 / RAINBOW

Despite the three-word post-block read-ahead model in v17, most compressed-data reads are synchronous within `rainbow_fast.c` and `rainbow_accurate.c`. **Full deferred input is not implemented.** It will require a bounded, grant-fed compressed-word FIFO and an interruptible/stepwise Huffman and RLE decoder that resumes only when input arrives. Retaining JPEG/RLE parser state, pending K-BUS grants, 16-line-block readiness and FIFO contents in save states is essential. Faking a zero byte on FIFO underflow or rewinding a partially decoded block would be incorrect.

See `docs/HARDWARE_TESTS_REQUIRED.md` for falsifiable hardware tests and the known sprite-regression concern.
