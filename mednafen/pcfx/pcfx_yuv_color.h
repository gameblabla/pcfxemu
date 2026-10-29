/* HuC6261 final DAC signal conversion for PC-FX emulator display.
 *
 * The DAC receives full-range Y, U and V, with unsigned U/V biased by 128.
 * KING, RAINBOW and palette graphics share this final conversion.
 *
 * The six constants below are the inverse of the conventional three-decimal
 * RGB->analog YUV matrix:
 *   Y =  0.299 R + 0.587 G + 0.114 B
 *   U = -0.147 R - 0.289 G + 0.436 B
 *   V =  0.615 R - 0.515 G - 0.100 B
 *
 * The small U->R and V->B terms arise from rounding the forward coefficients;
 * they are NOT hardware measurements of crosstalk. This is an analog-YUV
 * display approximation, not measured motherboard/encoder colorimetry.
 * Refer to docs/HARDWARE_TESTS_REQUIRED.md for calibration limitations.
 */
#ifndef PCFX_YUV_COLOR_H
#define PCFX_YUV_COLOR_H

#include <math.h>

typedef struct
{
 int r, g, b;
} PCFX_YUVColorOffsets;

/* The single supported final-output color transform. For the common default
 * floating-point mode (FE_TONEAREST), lrint rounds to nearest, ties-to-even.
 * The 65536-entry chroma offset table is constructed at initialization, not
 * per pixel. Keep the historical expression evaluation order for continuity.
 */
static inline PCFX_YUVColorOffsets PCFX_AnalogYUVOffsets(unsigned ur,
                                                         unsigned vr)
{
 const int u = (int)ur - 128;
 const int v = (int)vr - 128;
 PCFX_YUVColorOffsets out;
 out.r = (int)lrint(0 - 0.000039457070707 * u + 1.139827967171717 * v);
 out.g = (int)lrint(0 - 0.394610164141414 * u - 0.580500315656566 * v);
 out.b = (int)lrint(0 + 2.031999684343434 * u - 0.000481376262626 * v);
 return out;
}

static inline unsigned PCFX_YUVPalettePacked(unsigned yuv844)
{
 /* Palette YUV844: zero-fill low chroma nibbles, not sign-extend or remap. */
 return ((yuv844 & 0xFF00u) << 8) |
        ((yuv844 & 0x00F0u) << 8) |
        ((yuv844 & 0x000Fu) << 4);
}
#endif
