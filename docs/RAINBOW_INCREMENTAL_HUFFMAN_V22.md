# HuC6271 RAINBOW — grant-fed incremental JPEG Huffman parsing (v22)

Reference: official Hudson Soft HuC6272 documentation (`C6272_2.WRI`, §3.4), especially 16-raster blocks, REQR and read-ahead / dummy word rules. The *encoding syntax and exact Huffman quick-lookup tables* are inherited from the existing Mednafen RAINBOW accurate decoder. The official hardware documentation does not establish a physically sized Huffman FIFO or a per-symbol execution latency. Neither is invented here.

## Implementation

`mednafen/pcfx/rainbow_huffman_stream.h` implements a **transactional, resumable coefficient parser** alongside the existing pixel-producing JPEG decoder. It advances as v19-v21's KING K-BUS producer delivers compressed bytes, rather than waiting for a full image block before analyzing symbols. A DC or AC Huffman token and its signed amplitude commit only when *all* bits are available from real granted bytes. A literal entropy `FF` is accepted with its following stuffed byte together. Missing bytes cause an explicit stall, **never zero-filled state**.

Persistent state per in-flight JPEG strip includes the input byte cursor, bit reservoir and remaining bits, current of 16 columns, current of six DCT components, DC predictors (Y/U/V), AC zigzag position, current signed coefficient arrays, quantizer scale indicator, and rolling coefficient checksum. Quantizer header bytes continue to reside in v21's `RBJPEGQuant` state; no second KRAM read is performed.

`RBH_State` is serialized in KING's save-state section with a version/validity guard for loading older states. Its coefficient hash is a regression diagnostic only. All existing renderer output and raster timing decisions continue to use `rainbow_accurate.c` or `rainbow_fast.c`. **These backends remain block-atomic for JPEG/IDCT.** v22 does *not* claim hardware-cycle-accurate JPEG output, and the extra Huffman parse is intentionally redundant until it can safely supply output coefficients to a resumable IDCT/MCU backend.

The new module uses actual Huffman tables from the accurate decoder. The AC 0x10 zero-run and Y-DC 0x0F null-column instructions are handled, as are DC rescale opcodes. It intentionally does not yet emit an image, independently validate RAINBOW quantizer/IDCT math, or make an assertion about silicon FIFO capacity.

## Tests

- `tests/run_king_v22_huffman_stream_test.sh` generates a 16-column, 6-component zero-coefficient strip using the original Huffman LUTs and feeds it byte-by-byte. It verifies 96 decoded components, transactional stalls, and a copied-state resume. An additional strip has nonzero signed DC and AC amplitudes, which must change the coefficient checksum. It also tests split entropy `FF` stuffing.
- The v21 RAINBOW input and v20 RLE tests, plus the SCSI/ADPCM/K-BUS regression suites, pass.
- *Makeruna! Makendou Z*, BIOS cold boot for 1,600 emulated frames: **6,255 completed JPEG Huffman strips**, matching 6,255 grant-fed JPEG blocks; no invalid codes and zero reported compressed-input underflows. Its 1024×480 screenshot is pixel-identical to v21.
- Both accurate and fast video backends: 900-frame screenshot comparison is pixel-identical to v21.
- 480i dual-VDC/KING photographic homebrew: 2,400 frames, 1024×480, pixel-identical to v21.
- Tank 3D demo: 1,500-frame screenshot pixel-identical to v21; no intended V810/KRAM wait change.
- Save at 900 and resume 300 frames: screenshot identical to continuous 1,200-frame output. A v21 save state also loads under v22 with the same pixel output when presentation settings match.
- Headless and SDL3 built; SDL3 tested with console/offscreen support, **not** a desktop window server or physical PC-FX.

## Why this is not yet an asynchronous JPEG renderer

The coefficient parser is a second, stateful consumer of grant-fed input. The actual final picture is still decoded from the completed block in the original backend, so RAINBOW does not yet display partially completed MCU columns or correctly emulate Huffman/IDCT output deadlines under starvation.

The next step is to move the *coefficient consumer* into a backend API shared by accurate/fast rendering: finish an MCU's six components, apply the quantization/IDCT rules, and publish its 16-pixel column only when hardware output timing permits. Then remove the redundant block-atomic coefficient parse, guard against register changes during a strip, and test save-state restoration in the middle of an unfinished MCU. Only after the consumer is proven equivalent should the staging buffer be bounded to a testable FIFO size.
