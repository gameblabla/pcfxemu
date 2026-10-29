#ifndef PCFX_DOTWIDTH_H
#define PCFX_DOTWIDTH_H

/* Presentation only. The HuC6261 mixer must ALWAYS run at its 21.477MHz
 * reference clock when high-dotclock mode is enabled. A 256/341 output is
 * derived from that lossless internal signal, not fed back into TETSU. */
#include <stdint.h>
#include <string.h>

enum { PCFX_WIDTH_AUTO = 0, PCFX_WIDTH_DOTS = 1, PCFX_WIDTH_CLOCK = 2 };

/* The signal is sample-and-hold in the 1024 clock domain. A VDC-only edge
 * can only fall at multiples of 3; a KING/RAINBOW-only edge at multiples of 4.
 * Evidence of both types of actual visible edge is sufficient to retain the
 * 1024-clock signal in auto mode. A non-grid transition also needs retention
 * (e.g. a peripheral overlay). Transparent layers do NOT count as mixing. */
static inline int pcfx_dotwidth_choose(int mode, int source_width,
                                        const void *frame, int pitch,
                                        int lines, int bpp)
{
    if(source_width != 1024) return source_width;
    if(mode == PCFX_WIDTH_CLOCK) return 1024;
    if(mode == PCFX_WIDTH_DOTS) return 341;
    if(!frame || (bpp != 2 && bpp != 4) || pitch < 1024 || lines < 1)
        return 1024;
    int vdc_edges = 0, king_edges = 0;
    const uint8_t *pixels = (const uint8_t*)frame;
    for(int y = 0; y < lines; y++)
    {
        const uint8_t *row = pixels + (size_t)y * pitch * bpp;
        for(int x = 1; x < 1024; x++)
        {
            if(!memcmp(row + (size_t)x*bpp, row + (size_t)(x-1)*bpp, bpp))
                continue;
            /* A boundary on both grids (multiple of 12) is ambiguous and
             * never by itself proves that two clocks are contributing. */
            if(x % 3 == 0 && x % 4 != 0) vdc_edges = 1;
            if(x % 4 == 0 && x % 3 != 0) king_edges = 1;
            /* Other timing phase (or third hardware clock): don't lose it. */
            if(x % 3 != 0 && x % 4 != 0) return 1024;
            if(vdc_edges && king_edges) return 1024;
        }
    }
    return king_edges ? 256 : 341;
}

static inline void pcfx_dotwidth_downsample(void *destination,
                                            const void *source,
                                            int source_pitch, int height,
                                            int target_width, int bpp)
{
    uint8_t *dst = (uint8_t*)destination;
    const uint8_t *src = (const uint8_t*)source;
    if(target_width != 256 && target_width != 341) return;
    for(int y = 0; y < height; y++)
    {
        const uint8_t *s = src + (size_t)y * source_pitch * bpp;
        uint8_t *d = dst + (size_t)y * source_pitch * bpp;
        for(int x = 0; x < target_width; x++)
        {
            /* Sample at each physical pixel's clock-domain centre. 341
             * triples cover 1023 of 1024 ticks; KING's 256 quads cover all. */
            int clock_x = target_width == 256 ? 4*x + 2 : 3*x + 1;
            memcpy(d + (size_t)x*bpp, s + (size_t)clock_x*bpp, (size_t)bpp);
        }
    }
}

#endif
