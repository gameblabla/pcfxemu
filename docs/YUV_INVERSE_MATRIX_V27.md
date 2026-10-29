# PCFXemu v27 — single inverse-YUV transform using lrint()

## Change from v26

The v26 diagnostic profile `inverse-lrint` is now the *only* RGB presentation
matrix. The v25-simplified matrix, C-cast truncation matrix, runtime mode enum,
mode parser, and `PCFX_YUV_MATRIX` environment selector were removed.
The RGB output is produced after TETSU mixes KING, RAINBOW and other graphics.

The floating point offsets used for all 256 x 256 U/V values are:

```c
const int u = (int)ur - 128;
const int v = (int)vr - 128;
r = (int)lrint(0 - 0.000039457070707 * u + 1.139827967171717 * v);
g = (int)lrint(0 - 0.394610164141414 * u - 0.580500315656566 * v);
b = (int)lrint(0 + 2.031999684343434 * u - 0.000481376262626 * v);
```

These numbers invert the conventional rounded forward RGB->analog YUV matrix:

```
Y =  0.299 R + 0.587 G + 0.114 B
U = -0.147 R - 0.289 G + 0.436 B
V =  0.615 R - 0.515 G - 0.100 B
```

The small U->R and V->B components are numerical effects of rounding the
forward coefficients, not measured cross-channel coupling in the PC-FX.
`lrint()` uses the active floating point rounding mode; with the conventional
FE_TONEAREST mode, half-way values use nearest-even. The LUT is calculated
once, not per pixel. No hardware RGB fidelity claim is made: accurate PC-FX
analog gains/encoder/transfer still require real-console measurements.

## Validation and comparison

`tests/run_king_v27_yuv_matrix_test.sh` checks the inverse of the original
forward matrix, all 65,536 U/V combinations, neutral chroma, clamping, and
palette encoding. The other existing tests verify RAINBOW and K-BUS behavior.

A fresh, controlled v26 `inverse-lrint` game frame 1600 is supplied as
`captures/v26_inverse_lrint_reference_f1600.png`. Under the *same*
BIOS, game, save-directory initialization, frame count and horizontal
mode, the newly generated `captures/v27_default_f1600.png` is byte-for-byte
identical (same SHA-256). This is a direct emulator A/B test, not a
hardware capture.
Against v25's frame 1600, this produces differences in 1,156 of 491,520
pixels, with a maximum per-channel RGB change of one. This is a comparison
between emulator reconstruction algorithms, not a real-hardware calibration.

For a side-by-side reference, `captures/v25_f1600.png` is retained, but
no historical matrix implementation is included in the production source.

The paired captures were collected with both executables using:

```sh
--bios-dir /path/to/pcfx.rom --save-dir /path/to/fresh-save-dir \
--frames 1600 --horizontal clock --screenshot /path/to/capture.png \
"Makeruna! Makendou Z (Japan).chd"
```

`PCFX_YUV_MATRIX=inverse-lrint` was set only for v26. Do not set it
for v27; the environment option no longer exists. The included
`run-sdl3-offscreen.sh` is an offscreen test launcher, not a desktop
SDL3 distribution. An external desktop-capable SDL3 build is required
for a normal SDL3 window.
