/* HuC6261 final DAC signal conversion for PC-FX emulator display.
 *
 * The DAC receives full-range Y, U and V, with unsigned U/V biased by 128.
 * KING, RAINBOW and palette graphics share this final conversion.
 *
 * These coefficients invert the conventional three-decimal RGB-to-analog
 * YUV matrix. The small U-to-R and V-to-B terms come from rounding the
 * forward coefficients; they are not measurements of hardware crosstalk.
 */
#ifndef PCFX_YUV_COLOR_H
#define PCFX_YUV_COLOR_H

#include <math.h>

typedef struct
{
 int r, g, b;
} PCFX_YUVColorOffsets;

/* Keep the historical expression order for continuity. */
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
 /* Expand the palette's chroma nibbles by zero-filling their low bits. */
 return ((yuv844 & 0xFF00u) << 8) |
        ((yuv844 & 0x00F0u) << 8) |
        ((yuv844 & 0x000Fu) << 4);
}

#endif
