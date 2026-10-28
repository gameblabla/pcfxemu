#include "pcfx_interlace.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const char *pcfx_interlace_mode_name(PCFXInterlaceMode mode)
{
    switch(mode) {
        case PCFX_INTERLACE_BOB: return "bob (latest field)";
        case PCFX_INTERLACE_RAW: return "raw interlace (alternating scanlines)";
        case PCFX_INTERLACE_ADAPTIVE: return "motion-adaptive";
        case PCFX_INTERLACE_PAIR_LOCK: return "pair-locked weave";
        default: return "weave (both fields)";
    }
}

int pcfx_interlace_init(PCFXInterlaceState *s, size_t pitch, unsigned h)
{
    size_t n;
    if(!s || !pitch || !h || (h & 1u) || pitch > SIZE_MAX / h) return 0;
    memset(s, 0, sizeof(*s));
    n = pitch * h;
    s->previous[0] = malloc(n);
    s->previous[1] = malloc(n);
    s->pair_pending = calloc(1,n);
    s->pair_complete = calloc(1,n);
    if(!s->previous[0] || !s->previous[1] || !s->pair_pending || !s->pair_complete) {
        pcfx_interlace_destroy(s);
        return 0;
    }
    s->pitch_bytes = pitch;
    s->height = h;
    return 1;
}

void pcfx_interlace_reset(PCFXInterlaceState *s)
{
    if(s) { s->seen_mask = 0; s->pair_mask = 0; s->pair_valid = 0; }
}

void pcfx_interlace_destroy(PCFXInterlaceState *s)
{
    if(!s) return;
    free(s->previous[0]);
    free(s->previous[1]);
    free(s->pair_pending);
    free(s->pair_complete);
    memset(s, 0, sizeof(*s));
}

void pcfx_bob_field(void *dst, const void *src, size_t pitch,
                    unsigned h, int odd)
{
    const uint8_t *s = src;
    uint8_t *d = dst;
    unsigned parity = odd ? 0u : 1u;
    if(!s || !d || !pitch || h < 2u || (h & 1u)) return;
    for(unsigned y = 0; y < h; ++y) {
        unsigned source = (y & 1u) == parity ? y :
                          (parity ? (y + 1u < h ? y + 1u : y - 1u) : y - 1u);
        memcpy(d + (size_t)y*pitch, s + (size_t)source*pitch, pitch);
    }
}

/* Source pixel is little-endian RGB565 or a 32-bit frontend pixel with
   three color bytes and one alpha byte. Channel order does not affect SAD. */
static unsigned pixel_sad(const uint8_t *a, const uint8_t *b, unsigned bpp)
{
    unsigned sum = 0;
    if(bpp == 2) {
        uint16_t x = (uint16_t)a[0] | ((uint16_t)a[1] << 8);
        uint16_t y = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
        unsigned ar = ((x >> 11) & 31u) * 255u / 31u, br = ((y >> 11) & 31u) * 255u / 31u;
        unsigned ag = ((x >> 5) & 63u) * 255u / 63u, bg = ((y >> 5) & 63u) * 255u / 63u;
        unsigned ab = (x & 31u) * 255u / 31u, bb = (y & 31u) * 255u / 31u;
        return (ar > br ? ar - br : br - ar) +
               (ag > bg ? ag - bg : bg - ag) +
               (ab > bb ? ab - bb : bb - ab);
    }
    for(unsigned k=0; k<3; ++k)
        sum += a[k] > b[k] ? a[k]-b[k] : b[k]-a[k];
    return sum;
}

void pcfx_interlace_present(PCFXInterlaceState *state, void *dst, const void *src,
                            size_t pitch, unsigned h, unsigned bpp,
                            int odd, PCFXInterlaceMode mode)
{
    const uint8_t *s = src;
    uint8_t *d = dst;
    unsigned parity = odd ? 0u : 1u;
    unsigned width;
    if(!state || !s || !d || s == d || !pitch || h < 2u || (h & 1u) ||
       (bpp != 2 && bpp != 4) || pitch % bpp || pitch != state->pitch_bytes ||
       h != state->height) return;
    width = (unsigned)(pitch / bpp);

    if(mode == PCFX_INTERLACE_PAIR_LOCK) {
        /* The PC-FX paired-player changes source pictures on ODD fields.
           Never combine an old even field with a new odd field. Require
           odd -> even, and publish only after the latter completes. */
        if(parity == 0u) {
            for(unsigned y=0;y<h;y+=2u)
                memcpy(state->pair_pending+(size_t)y*pitch,s+(size_t)y*pitch,pitch);
            state->pair_mask = 1u;
        } else if(state->pair_mask == 1u) {
            for(unsigned y=1;y<h;y+=2u)
                memcpy(state->pair_pending+(size_t)y*pitch,s+(size_t)y*pitch,pitch);
            memcpy(state->pair_complete,state->pair_pending,pitch*h);
            state->pair_valid=1u;
            state->pair_mask=0u;
        }
        /* When pairing is not yet ready, hold the last *complete* pair.
           Before the first pair, bob rather than display black odd rows. */
        if(state->pair_valid) memcpy(d,state->pair_complete,pitch*h);
        else pcfx_bob_field(d,s,pitch,h,odd);
    }
    else if(mode == PCFX_INTERLACE_WEAVE) memcpy(d, s, pitch*h);
    else if(mode == PCFX_INTERLACE_BOB) pcfx_bob_field(d, s, pitch, h, odd);
    else if(mode == PCFX_INTERLACE_RAW) {
        for(unsigned y=0; y<h; ++y) {
            uint8_t *out = d + (size_t)y*pitch;
            if((y & 1u) == parity) memcpy(out, s + (size_t)y*pitch, pitch);
            else {
                memset(out, 0, pitch);
                /* Opaque black scanline. SDL3 may otherwise blend an
                   alpha-zero RGBA row with the window clear color. */
                if(bpp == 4) for(unsigned x=3; x<pitch; x+=4) out[x]=255;
            }
        }
    } else {
        /* Start from the newest parity; each missing row can be woven from
           the previous opposite field, or bobbed if that 8x4 field-line tile
           has moved compared with the previous SAME-parity field. A static
           tile retains full 480-line detail. The threshold is per RGB pixel
           (sum of three channel differences, 0..765). */
        const uint8_t *prev = state->previous[parity];
        int valid = !!(state->seen_mask & (1u << parity));
        memcpy(d, s, pitch*h);
        for(unsigned block_y=0; block_y<h/2u; block_y+=4u) {
            unsigned end_y = block_y+4u < h/2u ? block_y+4u : h/2u;
            for(unsigned block_x=0; block_x<width; block_x+=8u) {
                unsigned end_x = block_x+8u < width ? block_x+8u : width;
                uint64_t sad = 0;
                unsigned n=0;
                int moving;
                if(valid) {
                    for(unsigned fy=block_y; fy<end_y; ++fy) {
                        unsigned sy = 2u*fy+parity;
                        for(unsigned x=block_x; x<end_x; ++x) {
                            size_t pos = (size_t)sy*pitch + (size_t)x*bpp;
                            sad += pixel_sad(s+pos, prev+pos, bpp);
                            ++n;
                        }
                    }
                }
                moving = !valid || sad > (uint64_t)n * 60u;
                if(moving) {
                    for(unsigned fy=block_y; fy<end_y; ++fy) {
                        unsigned out_y = 2u*fy+(parity^1u);
                        unsigned source_y = 2u*fy+parity;
                        /* Bob missing lines from newest field only. */
                        memcpy(d + (size_t)out_y*pitch + (size_t)block_x*bpp,
                               s + (size_t)source_y*pitch + (size_t)block_x*bpp,
                               (end_x-block_x)*bpp);
                    }
                }
            }
        }
    }
    /* Never read the opposite parity into the temporal history: that field
       may come from a different source picture. A repeated UI draw should
       not call this function again; it would erase motion history. */
    for(unsigned y=parity; y<h; y+=2u)
        memcpy(state->previous[parity] + (size_t)y*pitch,
               s + (size_t)y*pitch, pitch);
    state->seen_mask |= 1u << parity;
}
