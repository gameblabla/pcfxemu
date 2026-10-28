#ifndef PCFX_SDL3_INTERLACE_H
#define PCFX_SDL3_INTERLACE_H

#include <stddef.h>
#include <stdint.h>

/* These are DISPLAY filters only. TETSU field order and emulated KRAM are not modified. */
typedef enum PCFXInterlaceMode {
    PCFX_INTERLACE_WEAVE = 0,      /* newest odd + even rows, temporal combing possible */
    PCFX_INTERLACE_BOB = 1,        /* current field doubled to every output scanline */
    PCFX_INTERLACE_RAW = 2,        /* current physical field, opposite rows black */
    PCFX_INTERLACE_ADAPTIVE = 3,   /* weave static blocks; bob moving blocks */
    PCFX_INTERLACE_PAIR_LOCK = 4   /* present only completed odd/even pairs */
} PCFXInterlaceMode;

typedef struct PCFXInterlaceState {
    uint8_t *previous[2];         /* previous frame of EACH field parity */
    size_t pitch_bytes;
    unsigned height;
    unsigned seen_mask;
    uint8_t *pair_pending;         /* accumulate actual current field rows */
    uint8_t *pair_complete;        /* last completed two-parity picture */
    unsigned pair_mask;
    unsigned pair_valid;
} PCFXInterlaceState;

const char *pcfx_interlace_mode_name(PCFXInterlaceMode mode);
int pcfx_interlace_init(PCFXInterlaceState *state, size_t pitch_bytes, unsigned height);
void pcfx_interlace_reset(PCFXInterlaceState *state);
void pcfx_interlace_destroy(PCFXInterlaceState *state);

/* dst must not alias src; pitch must include horizontal padding. Supports RGB565
   and 32-bit RGB(A), with alpha in the high byte of the native pixel. The
   state must be updated exactly ONCE per newly emulated field, never per UI
   redraw; pcfx_interlace_present() saves the newest field for comparisons. */
void pcfx_interlace_present(PCFXInterlaceState *state, void *dst, const void *src,
                            size_t pitch_bytes, unsigned height, unsigned bytes_per_pixel,
                            int odd, PCFXInterlaceMode mode);

/* Kept as a backwards-compatible, pure bob helper. */
void pcfx_bob_field(void *dst, const void *src, size_t pitch_bytes,
                    unsigned output_height, int odd);
#endif
