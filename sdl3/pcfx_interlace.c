#include "pcfx_interlace.h"
#include <stdint.h>
#include <string.h>

void pcfx_bob_field(void *dst, const void *src, size_t pitch_bytes,
                    unsigned output_height, int odd)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    unsigned parity = odd ? 0u : 1u;
    if(!s || !d || !pitch_bytes || output_height < 2u || (output_height & 1u)) return;
    for(unsigned y = 0; y < output_height / 2u; ++y)
    {
        const uint8_t *field_line = s + ((size_t)(2u * y + parity) * pitch_bytes);
        memcpy(d + (size_t)(2u * y) * pitch_bytes, field_line, pitch_bytes);
        memcpy(d + (size_t)(2u * y + 1u) * pitch_bytes, field_line, pitch_bytes);
    }
}
