# PCFXemu v23 — grant-fed JPEG MCU → IDCT/luma integration

## Scope and specification

The official Hudson Soft HuC6272 documentation (part 2, §3.4) defines HuC6271/Rainbow compressed-data transfers, 16-scanline blocks and read-ahead/dummy-word handling. It does **not** specify an externally measurable Huffman-symbol throughput, silicon FIFO depth or IDCT pipeline latency. No guessed constants have been added.

This revision advances the grant-fed JPEG pipeline from v22's independent coefficient parser to a **complete 16×16 luminance reconstruction per MCU column**. It retains all six 8×8 coefficient blocks until the column has finished, then applies dequantization and the same IDCT variant used by the active renderer. Results are placed in a separate 256×16 luminance staging image for comparison. Pixel production is incremental as a *diagnostic*; the existing accurate/fast pixel renderer remains authoritative because a small number of strip-level differences are unresolved.

### Source changes

- `rainbow_huffman_stream.h`: `RBH_AdvanceColumns()` passes completed six-block MCUs to a callback; partial Huffman symbol consumption remains transactional. DC/AC state and unfinished MCU blocks survive grants. Adds `RBH_DrainTrailingRescales()` for quantization control codes at the end of the strip. The original `RBH_Advance()` interface remains for callers that do not need columns.
- `rainbow_mcu_luma.h`: dequantizes four luma 8×8 blocks, applies the active decoder's accurate `IDCT` or fast `j_rev_dct`, and assembles a 16×16 luma column. Chroma interpolation is not part of this comparison.
- `king.c`: tracks quantizer base tables across F8/FF strips, preserves zero as a **valid quantizer scale**, accumulates MCU columns, and compares the finished column image with the established renderer. The output image is not altered. New diagnostic state and counters are serialized, with a version guard for older saves.
- `rainbow_accurate.c`, `rainbow_fast.c`, `rainbow.c`: read-only luminance comparison interface for the decoder's completed buffer.

### Default behavior

The double-IDCT comparison is **disabled by default** to avoid unnecessary host CPU work. Set `PCFX_RAINBOW_MCU_COMPARE=1` to enable it and `PCFX_RAINBOW_STATS=1` to show counters on exit:

```sh
PCFX_RAINBOW_MCU_COMPARE=1 PCFX_RAINBOW_STATS=1 \
  ./pcfx-headless --bios-dir /path/to/pcfx.rom --auto-run \
  --frames 1600 'Makeruna! Makendou Z (Japan).chd'
```

The unified K-BUS scheduler remains the sole timing model; neither video-clock presentation nor CPU cycles were changed.

## Measurements

Cold-boot from the supplied CHD with supplied BIOS, using the accurate backend at frame 1600:

| Item | Result |
|---|---:|
| Completed JPEG strips compared | 6,240 |
| Fully matching strips | 6,236 |
| Reconstructed image columns | 99,840 |
| Compared luminance samples | 25,559,040 |
| Differing samples | 11,950 |
| Strip-level exceptions | 4 |

For the accurate decoder at frame 1200: 1,770 compared strips, 1,766 exact, 11,950 differing samples among 7,249,920 comparisons. The fast decoder at frame 900 uses its own `j_rev_dct` and also has 1,766 matching strips out of 1,770, with 11,946 differing luminance samples. Four exceptional strips appear to be associated with special compressed-block/output handling; the exact cause is not yet established.

The change does **not** demonstrate hardware-accurate display/IDCT pipeline deadlines; that work needs official specification analysis and/or hardware capture.

## Runtime regressions

- Accurate Makendou, 1600 frames: screenshot pixel-identical to v22 with `--horizontal clock`.
- Fast Makendou, 900 frames: screenshot pixel-identical to v22 at its selected output resolution.
- Tank demo, 1500 frames: screenshot pixel-identical to v22.
- Photo homebrew, 2400 frames: screenshot pixel-identical to v22 at 1024×480.
- Save 900 frames, load + 300 frames: screenshot matches continuous 1200 frames.
- Existing v22 regression suite plus a new MCU/IDCT/quantizer unit test passes.
- Headless and SDL3 Linux/offscreen builds succeed. Desktop-window behavior and physical console remain untested.

## Remaining work before switching renderers

1. Diagnose the four differing JPEG strips (quantizer rescale across unusual control sequences, null-column runs or output-buffer control). Never silently replace these pixels with guessed values.
2. Extend the incremental path to U/V coefficient IDCT and chroma interpolation, including the fast-backend semantics, then check complete 32-bit YUV output against both existing backends.
3. Once output parity is established, publish completed 16×16 MCU columns to the real buffer at modeled hardware deadlines; explicitly test starvation and mid-MCU save states.
4. Limit the staging buffer only after its real required depth and grant-latency behavior have been characterized. The Hudson document gives block/prefetch rules, but not a directly justified physical FIFO size.
