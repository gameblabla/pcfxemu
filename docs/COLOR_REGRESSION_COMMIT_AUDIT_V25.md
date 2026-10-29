# PCFXemu v25: commit d344a0ad color regression and RAINBOW concealment audit

Source commit: https://github.com/gameblabla/pcfxemu/commit/d344a0ad8ae24a5f381b61652b69e601cf48db4a
Commit title: **Fix RAINBOW colors and damaged FMV strips** (2026-10-04).

## The commit contains two independent changes

1. It changed the shared `mednafen/pcfx/king.c::RebuildUVLUT()` transformation from approximate analog YUV to JPEG/BT.601 full-range YCbCr. Crucially, this conversion is applied **after** TETSU mixes the RAINBOW, KING and VDC/palette planes. A JPEG-style inverse matrix is not uniquely implied by RAINBOW's JPEG-like compressed input. The change therefore affects all planes.
2. It introduced `rainbow_confetti.inc`, a heuristic which replaces a full 256x16 decoded YUV strip with an earlier clean-looking one based on horizontal noise. It is not specified by NEC/Hudson and can erase legitimate high-frequency content. Its state was not part of the decoder's state serialization. It is a concealment workaround, not an emulation of the HuC6271's compressed-data FIFO or synchronization.

## Official HuC6261 ground truth

`DOCUMENTATION/ORIGINAL_JPN/C6261.WRI`, section 2.8, and its translation, specify three linear eight-bit Y/U/V DAC outputs, with U=V=0x80 representing neutral chroma, and palette U/V nibble expansion by appending four zero bits. Both KING and RAINBOW use the shared output stage.

**The documentation does NOT specify the complete retail motherboard's output colorimetry:** DAC gain and offset, YUV-to-RGB transform through the downstream video encoder, component/S-Video analog bandwidth, display EOTF, or a complete sRGB color profile. There is therefore no justified way to label either the old analog-YUV matrix or the JPEG YCbCr matrix as a calibrated exact PC-FX RGB transform.

The v24 analog-YUV matrix is retained in v25 *as an explicit approximation* because it preserves the documented Y/U/V signal interpretation across all planes. It is not a guarantee of physically accurate KING palette or RAINBOW color. Do not reintroduce the JPEG coefficients merely because a particular FMV scene subjectively looks more saturated.

## v25 change

The algorithmic `RB_ConfettiRepair` is disabled by default on both decoder backends. The exact historical behavior can be enabled for investigation with `PCFX_RAINBOW_CONCEALMENT=1`. This switch changes *only* decoded strip concealment, not the K-BUS/CPU timing model, YUV matrix, JPEG entropy parsing, output resolution, or other video paths. It is not a legacy timing mode.

## Regression evidence

- Verified `d344a0ad` via the GitHub repository commit and source diff.
- `tests/run_rainbow_concealment_test.sh`: proves the disabled default preserves a synthetic legitimate high-frequency YUV strip, while opt-in historical concealment replaces it with an earlier strip.
- `tests/run_king_v24_yuv_color_test.sh`: passes.
- Both headless and SDL3 compile; SDL3 `--help` launches through offscreen runtime.
- *Makeruna! Makendou Z*, cold boot + 1,600 emulated frames, HuC6261 1024-clock output: **pixel-identical** between v25 default, v25 historical concealment and the earlier v24 reference at the same frame. This does NOT prove the heuristic never fires in other games or frames.
- No physical-console colorimetry was measured.

## Next investigation: reliable KING+RAINBOW colors

1. Create a retail PC-FX test that outputs known YUV888 and palette YUV844 tuples from KING, RAINBOW and VDC; save the source tuples independently of RGB screenshots.
2. Record raw component YUV DAC outputs, or use a **calibrated** capture chain with the analog transfer function documented. Preserve full/limited range handling and the correct component order; avoid estimating chroma gains from subjective screenshots.
3. Measure grayscale (Y=0,16,64,128,192,235,255 with U=V=128), signed chroma axis sweeps, clipping/gamut, chroma frequency response and phase. Test both KING direct YUV and palette YUV844, because nibble expansion alone reduces chroma granularity.
4. Solve a transform from **measured** signal responses; if necessary, model the downstream video encoder and display separately. Apply the correction only after TETSU composition.
5. Compare HuC6271's reconstructed **Y, U, V** planes independently of final RGB, including chroma subsampling, quantizer scale 0, IDCT and interp boundary conditions. The v23 comparator chiefly validates Y and has unresolved exceptional strips.
6. For strip errors, log compressed-stream starvation/invalid symbols and K-BUS request/grant order. Do not silently replace valid imagery because it has high spatial frequency.

The v25 change intentionally does not claim to solve the remaining hardware color-accuracy problem; it removes an unsupported image-altering heuristic from the default path while preserving a controlled A/B debugging switch.
