# HuC6271 RAINBOW — grant-fed JPEG header and entropy input (v21)

**Reference:** Hudson Soft HuC6272 documentation, `C6272_2.WRI`, §3.4.2, especially the 16-raster transfer arrangement, REQR / compressed-data access, the additional two bytes inside the declared block length, and three subsequent zero guard words. The Huffman, quantization and FF-stuffing rules also follow the already implemented RAINBOW decoder's byte protocol; do not infer an undocumented electrical timing rule from them.

## Implemented

The F8 (reuse quantizer) and FF (new quantization tables) paths now include a **grant-driven, resumable JPEG input parser** in `mednafen/pcfx/king.c`:

- Recognizes block headers without reading ahead from live KRAM.
- Captures each of FF's 128 quantization bytes incrementally, without misinterpreting a legal `0xFF` quantizer byte as entropy stuffing.
- Walks the encoded entropy bytes in grant-completion order and atomically consumes an entropy `0xFF` and its following stored byte. A split pair waits until the next granted word arrives, leaving the input cursor unchanged.
- Maintains per-block input cursor, entropy end, current quantization bytes, byte count and rolling digest in the serialized KING state, including tests for split-word grant behavior.
- Exposes JPEG input counters under `PCFX_RAINBOW_STATS=1` without changing emulated CPU timing or the existing RLE path.

**Important limitation:** This parser is an input-side staging/progress mechanism, **not** a JPEG Huffman/IDCT state machine. The accurate and fast renderers are still called with a fully captured JPEG block. Hence v21 does **not** make coefficient parsing, predictors, IDCT or partial image output resumable, and does not yet have a hardware-sized bounded FIFO. The 128 KiB staging allocation remains an emulator implementation detail, not a claim about physical HuC6271 memory.

The modeled four-CPU-clock K-BUS service quantum, REQR waveform, JPEG decode speed and 16H starvation/output behavior have not been calibrated to hardware. In particular, a visually correct full-game frame does not demonstrate cycle-accurate JPEG decoding.

## Verification (2026-10-29)

- New `tests/run_king_v21_jpeg_stream_test.sh`: FF F8 partial entropy pair split across granted 16-bit KRAM words; FF FF 128-byte quantization header with a legitimate `0xFF` quantizer; no early word visibility or invented data.
- Prior v20 incremental RLE and v17–v18 K-BUS/ADPCM/SCSI tests pass.
- *Makeruna! Makendou Z* cold boot, 1,600 frames, accurate and fast RAINBOW backends: 6,255 JPEG blocks, 11,181,231 entropy bytes, 57,283 stuffed byte pairs and 28,579 split-pair waits; zero reported underflows. Screenshots byte-identical to v20 under matching command lines.
- 480i KING+dual-VDC homebrew cold boot, 2,400 frames: pixel-identical to v20 at 1024×480.
- Tank demo cold boot, 1,500 frames: pixel-identical to v20.
- *Makendou Z* state continuity: save at 900 frames + 300 resumed frames produces same image as 1,200 continuous frames.
- Headless and SDL3 (offscreen-capable library) builds pass; desktop-window behavior not independently verified.

## Next implementation steps

1. Introduce backend-owned, serialized Huffman bit reservoirs, DC predictors, quantizer scaling state, component / coefficient indices and partial columns. Pause a symbol without fabricating zero bits when a grant has not completed. Preserve both fast/accurate backends.
2. Make compressed data a bounded producer/consumer FIFO, with a `-REQR` watermark policy. Do not assign a physical FIFO capacity until measured.
3. Test a stall in the middle of a DC code, an AC symbol and an FF-escape; verify the resulting image and save-state continuation against the block-atomic reference.
4. Continue to avoid invented arbitration phases; measure real-console REQR, compressed-word service phases and JPEG display deadline under SCSI/ADPCM/BG contention.
