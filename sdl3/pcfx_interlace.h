#ifndef PCFX_SDL3_INTERLACE_H
#define PCFX_SDL3_INTERLACE_H
#include <stddef.h>
/* Bob a 480-line interleaved pixel surface. Source and destination may not
   overlap. pitch_bytes contains one FULL row including any horizontal padding.
   odd: field stored at rows 0,2,...; even: rows 1,3,... .
   The current field is copied to BOTH destination rows, never synthesized
   by blending against an older opposite-parity field. */
void pcfx_bob_field(void *dst, const void *src, size_t pitch_bytes,
                    unsigned output_height, int odd);
#endif
