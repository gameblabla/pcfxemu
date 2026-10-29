# v24: HuC6261 color-space audit and display reconstruction

## Primary references (official hardware documentation)

- `DOCUMENTATION/ORIGINAL_JPN/C6261.WRI`, HuC6261, §2.2 (palette RAM), §2.7 (320-dot mode), and **§2.8 (YUV output)**.
- `DOCUMENTATION/ENGLISH_TRANSLATION/C6261/C6261.md`, same sections for easier searching.
- `DOCUMENTATION/ENGLISH_TRANSLATION/C6272_2/C6272_2.md`, KING direct YUV844/YUV888 background formats.

HuC6261 produces three *analog* eight-bit DAC signals, Y, U, and V. Y uses 0x00–0xFF; U and V use 0x80 as neutral, with color differences of opposite sign on either side. In palette mode the four-bit U and V fields are expanded by **appending four zero bits**, so neutral is 0x8 -> 0x80. All VDC, KING and RAINBOW layers pass through the same final mixer/DAC path.

## Defect found

`mednafen/pcfx/king.c:RebuildUVLUT()` used JPEG full-range YCbCr inverse coefficients for the **shared** YUV output:

```
R = Y + 1.402000 V
G = Y - 0.344136 U - 0.714136 V
B = Y + 1.772000 U
```

Those coefficients may make sense for decoding typical JPEG YCbCr into RGB directly. Here they were applied to the **HuC6261's hardware YUV output**, including palette-based VDC/KING, which is not described as a JPEG-to-RGB decoder in the documentation.

v24 uses standard analog YUV coefficients as the *display reconstruction approximation*:

```
R = Y + 1.13983 V
G = Y - 0.39465 U - 0.58060 V
B = Y + 2.03211 U
```

Where U and V are the unsigned eight-bit DAC data minus 128. Saturation is not arbitrarily boosted. All output modes (headless, SDL3, RGBA8888, RGB565, 256/341-dot, 1024 clock) share the same lookup.

The color-space signal type, range and neutral point are official; **these precise RGB coefficients are conventional YUV reconstruction coefficients, not numbers measured from a PC-FX motherboard or quoted in the NEC/Hudson notes**. The shift corrects a source-space mismatch, not the complete analog transfer function.

## Verified tests

- `tests/run_king_v24_yuv_color_test.sh`: grayscale neutral, chroma sign/strength, YUV844 palette nibble expansion.
- Entire pre-existing `tests/run_v23_regression.sh` chain: pass.
- Headless RGBA8888, RGB565 and SDL3 frontend: compile. SDL3 offscreen `--help` starts using shipped SDL3 runtime.
- *Makeruna! Makendou Z*, 1600-frame BIOS cold boot: both v23 and v24 reach the same movie frame (1024×480). 485,246/491,520 RGB pixels differ (98.7%); mean absolute channel delta = 2.846/255; largest delta 16. This is a **small color transform, not proof the footage is now hardware-matched**.
- KING+VDC 480i still, same saved state + 20 frames: image dimensions 1024×480; mean absolute channel delta 5.07/255. The program/framebuffer layout is unchanged.
- Tank, same state + 100 frames: differences are limited to color presentation (3012/61440 pixels in the sampled 256×240 frame); RAM dump SHA-256 matches exactly between v23 and v24. No CPU/KRAM timing altered.
- RGB565 rendering versus 8888: mean absolute output-channel delta ~2.02, maximum 7, consistent with 5/6-bit RGB quantization.

## What this does **not** establish

- That the *subjective washed-out appearance* is completely corrected. No measured analog output or colorimetric hardware reference was supplied.
- Whether the console's video encoder converts YUV into NTSC component signals with exactly these conventional coefficients, nor its DAC gains, offsets, RGB gamma, chroma low-pass, composite modulation, or S-Video amplifier response.
- Full RAINBOW chroma parity. The v23 incremental-IDCT comparator checks **luma only**, and four exceptional strips still disagree; the old render backend remains responsible for pixels.
- Whether the SDL3 display's ICC/HDR/OS color-management pipeline maps sRGB identically to the screenshot PNG. The core emits RGB888 data and SDL3 displays its texture; offscreen startup was verified, not a calibrated physical monitor.

These are tracked in `docs/HARDWARE_TESTS_REQUIRED.md` below.
