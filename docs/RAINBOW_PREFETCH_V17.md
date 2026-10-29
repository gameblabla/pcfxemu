# v17 — HuC6271/Rainbow three-word KRAM read-ahead

Date: 2026-10-29. Baseline: PCFXemu v16. The **primary reference** is Hudson Soft's original Japanese `DOCUMENTATION/ORIGINAL_JPN/C6272_2.WRI`, §3.4.2(4). The English translation (`DOCUMENTATION/ENGLISH_TRANSLATION/C6272_2/C6272_2.md`) says that the KING has already read following KRAM words at the time HuC6271 deasserts `-REQR` after a 16-line block. Three extra zero words after **each block** are therefore mandatory for correctly concatenated streams. A block is decoded/displayed with an approximately 16-raster pipeline offset.

## Implemented and tested

* KING reserves *three* prospective following compressed-data **16-bit words** after a RAINBOW block is consumed. They share the same per-bank K-BUS reservation and priority ordering as SCSI/ADPCM, and their slot reservations carry unique tags when displaced by a later higher-priority request.
* The KRAM word value is captured on the simulated **grant completion**, not at the request. Its physical page, word address, grant timestamp and value are saved in emulator states. Before a new configured source-address start, outstanding read-ahead is invalidated.
* `KING_RB_Fetch()` consumes the saved grant-time word if the decoder reaches its address; this does not re-charge another K-BUS grant. If no prefetched data applies, the existing synchronous read is preserved.
* Both accurate and fast RAINBOW backends use the common `KING_RB_Fetch` path; the code does not require decoder-specific stream changes.
* `PCFX_RAINBOW_STATS=1` prints read-ahead request, completed grant, consumed word and eager word counts on emulator shutdown. Off by default.

## What it deliberately does NOT implement

**This is NOT fully deferred compressed-data transfer.** The majority of input words continue to be fetched by a synchronous decoder callback. The HuC6271 JPEG bitstream parser needs a resumable reader and a bus-produced FIFO, with `-REQR`-paced demand and the buffer valid/underflow state persisted in save states. A stalled FIFO must pause parsing; it cannot return fabricated zero bytes. The number and phase of physical bus read-ahead accesses beyond the documented 3-word guard are still unknown. Current model starts three-word read-ahead after the host decoder returns, not on a measured `-REQR` edge. That is an **approximation** and must not be marketed as hardware-accurate prefetch timing.

## Focused tests

`tests/run_king_v17_rainbow_ahead_test.sh` verifies:

1. Three word requests are granted independently, with KRAM data sampled only after a grant.
2. HBlank refresh displaces a prefetched read; the grant deadline moves.
3. A CPU/memory write after the grant does not retroactively change the saved word.
4. HBlank RAINBOW priority outranks SCSI: RAINBOW sees the old word, and SCSI's deferred KRAM update commits subsequently.
5. Cancelled read-ahead does not leak into a new source-address stream.

The complete v16 suite also passes, including deferred SCSI and ADPCM, KING/TETSU, interlace and 1024-clock tests.

## Game test: Makeruna! Makendou Z (Japan)

Use the user's CHD (not included in this distribution) and their own BIOS:

```sh
PCFX_RAINBOW_STATS=1 ./pcfx-headless \
  --bios-dir /path/to/pcfx.rom --auto-run --frames 1600 \
  --screenshot makendou.png 'Makeruna! Makendou Z (Japan).chd'
```

Observed over 1600 emulated frames:

| Counter | v17 measurement |
|---|---:|
| Three-word read-ahead requested | 37,215 |
| Granted read-ahead words | 25,381 |
| Consumed prefetched words | 20,965 |
| Other word reads (eager decoder) | 6,898,464 |

The disparity is intentional: read-ahead is only a small part of the compressed input pipeline. The 2400-frame v16 and v17 screenshots match pixel-for-pixel at **1024×480**. This establishes **functional regression compatibility**, not bitstream-to-bus timing correctness. A BIOS-start screenshot at 800 without automatic RUN is not a RAINBOW regression; the animated sequence was reached with `--auto-run`.

Other checks: 480i photographic still 1024x480 output byte-identical to v16; 300-frame tank snapshot byte-identical; both headless and SDL3 (offscreen) builds; unit tests pass. Physical hardware was not available for testing.

## Important unresolved defect discovered during the game run

The existing v16/v17 SCSI DMA pending-word queue occasionally reaches 128 outstanding words during this CHD's CD loading and prints `KING K-BUS: SCSI DMA pending queue exhausted`. Its fallback writes eagerly, breaking strict deferred-visibility semantics. This *pre-existing* problem must be addressed with correct SCSI REQ/ACK backpressure, not a larger arbitrary FIFO or a made-up stall. The affected boot path therefore does **not** yet prove full SCSI ↔ RAINBOW ordering. See `docs/HARDWARE_TESTS_REQUIRED.md`.

## Next implementation milestone

Split the HuC6271 input into (a) a K-BUS-granted 16-bit-word producer, (b) a bounded `-REQR`-driven FIFO, (c) a resumable entropy/RLE decoder that waits on missing input without rewinding or fabricating bytes, and (d) precise 16H block-ready/buffer-swap events. Save the producer, FIFO, Huffman/quantizer, and pending-grant state. Confirm on this title and separate RLE/JPEG fixtures before removing synchronous reads. Do not move arbitrary work into a later field simply to hide stalls.
