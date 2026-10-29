# HuC6271 RAINBOW — resumable RLE decoding (v20)

**Official reference:** Hudson Soft's `DOCUMENTATION/ORIGINAL_JPN/C6272_2.WRI`, HuC6272 section 3.4.2, including the compressed-data request (`-REQR`), 16H image blocks, read-ahead and the three zero trailing guard words. The corresponding English translation is `DOCUMENTATION/ENGLISH_TRANSLATION/C6272_2/C6272_2.md`.

## Implementation

v19 already sampled compressed words at modeled K-BUS grant completion, but delayed all decoding until the complete payload and its guard bytes had arrived. v20 **processes RLE F0–F3 tokens incrementally**. It retains v19's grant-fed producer and staged input; only a complete token is consumed. A count or stuffed byte that has not arrived yet leaves the token and decoded pixel position unchanged. Incremental RLE decoding is supported by both accurate and fast RAINBOW frontends using the same backend output buffers as the old implementation.

The RLE parser preserves the active block, staged input, token cursor, payload end, output pixel position, token counters and completed blocks in KING save states. The RAINBOW backends already save their two decode buffers. The pending bus grant remains scheduled by the unified K-BUS scheduler, so a higher-priority request can postpone data delivery without forcing zero bytes into the decoder. The background/RLE output format and the 16-line buffer-swap procedure are unchanged.

**Boundary fix:** the original RLE decoder may complete a run by reading up to the two inner dummy bytes beyond the nominal entropy payload. Tokens that terminate within that allowance are valid; the extra six external guard bytes are never treated as token input. The initial v20 implementation erroneously rejected these tokens and generated 1,976 false underflows in *Makendou Z*. The corrected implementation shows zero underflows and output identical to v19.

**This is not a complete hardware-accurate HuC6271.** JPEG F8/FF still decodes atomically once the block is fully staged. The 128 KiB staging allocation, K-BUS four-CPU-clock grant quantum, exact -REQR assertion phase and physical input FIFO capacity are emulator implementation choices, not stated hardware constants. The official documentation does not imply the chip itself maintains a 128 KiB FIFO. RLE token atomicity is a conservative emulation strategy; actual per-cycle token execution timing remains unverified. The decoder also still retains the older bounded malformed-packet fallback for other formats.

## Tests (October 29, 2026)

- `tests/run_king_v20_rainbow_incremental_test.sh`: priority displacement, no visibility before grant, two RLE runs emitted before block completion, count-byte stuffing split across two K-BUS words, no early emit, and valid completion after dummy/guard words.
- Existing v17 read-ahead, v18 SCSI backpressure, v16 ADPCM and v15 K-BUS priority tests passed.
- *Makeruna! Makendou Z*: accurate backend cold boot, 2,400 fields. 19,127,415 word grants, **6,150 RLE blocks and 1,779,574 RLE tokens processed incrementally**, zero input underflows; PNG byte-for-byte identical to v19 on matched launch.
- Fast backend cold boot, 1,600 fields: zero underflows; byte-for-byte identical to v19 fast screenshot.
- Save/load: 900 frames plus saved state plus 300 frames gave a PNG identical to an uninterrupted 1,200-frame run.
- Tank demo cold boot, 1,500 frames: screenshot byte-identical to v19.
- 480i photographic homebrew, 2,400 frames: byte-identical to v19 under identical BIOS, CUE path and launch arguments. An older reference captured with different startup context differed at 65 pixels; that old reference is not used to claim a regression.
- Headless and SDL3 compile successfully with CHD support. Physical console validation has not been performed.

## Remaining priorities

1. Add a bounded input FIFO with watermarks and an explicitly modeled -REQR handshake. Do not equate the existing 128 KiB staging buffer with physical silicon storage.
2. Convert JPEG Huffman parsing and coefficients to a genuine resumable state machine: 8/9/12-bit lookahead, pending decoded coefficients, bit reservoir, quantization table updates, predictor state, IDCT/output column completion, and 16H buffer handoff.
3. Test stalls inside a JPEG DC/AC symbol and restart from a saved mid-symbol state, with SCSI/ADPCM collisions to the same KRAM bank.
4. Calibrate request phase, internal FIFO depth, and starvation response on physical PC-FX hardware. Keep these in `HARDWARE_TESTS_REQUIRED.md` until measured.
