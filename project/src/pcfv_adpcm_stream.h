#ifndef PCFV_ADPCM_STREAM_H
#define PCFV_ADPCM_STREAM_H

/* PCFV audio backend: HuC6230 4-bit ADPCM played by KING channel 0 straight
   from a two-half ring in KRAM.  Implements pcfx_pcfv_internal.h.

   Stream contract (tools/pcfv_adpcm.py writes it; the original pcfv_encode
   layout is the same):
     - the audio is cut into blocks of one ring half (65536 bytes);
     - the preroll before frame 0 holds whole blocks (normally blocks 0 and 1);
     - every later block is one refill chunk after the video frame at which it
       becomes due, so the drive only makes short seeks for audio;
     - nibbles are stored low nibble first, the order KING feeds the decoder.
   Block j always lives in ring half (j & 1).  When the hardware finishes a
   half it raises a status bit (REG.53: bit1 = half 0 done, bit0 = half 1
   done); the player refills that half with block j + 2 through its
   non-blocking CD -> KRAM DMA.  No CPU copy and no decode on the V810.

   Timing (vendor/pcfxemu soundbox.c / king.c): the ADPCM clock is the
   21.477 MHz master / 682.5 = 31468.5 Hz, i.e. exactly two samples per
   1365-clock line and 524 per 262-line field; rate codes 1..3 halve it.  The
   player counts fields for a sample-exact clock and re-anchors it on every
   half boundary the hardware reports.  Emulator-derived; unmeasured on a
   retail console. */

#include "pcfx_pcfv_internal.h"

#define PCFV_ADPCM_KRAM_WORD_ADDR 0x00020000u /* ring base, KRAM page 0 */
#define PCFV_ADPCM_HALF_BYTES     65536u
#define PCFV_ADPCM_HALF_WORDS     (PCFV_ADPCM_HALF_BYTES / 2u)
#define PCFV_ADPCM_HALF_SECTORS   ((PCFV_ADPCM_HALF_BYTES + PCFV_SECTOR_SIZE - 1u) / PCFV_SECTOR_SIZE)
#define PCFV_ADPCM_RING_WORDS     (PCFV_ADPCM_HALF_WORDS * 2u)
#define PCFV_ADPCM_HALF_SAMPLES   (PCFV_ADPCM_HALF_BYTES * 2u)
/* Start/resume addresses (REG.58) have 256-word granularity. */
#define PCFV_ADPCM_START_ALIGN_BYTES 512u

/* Sample rate by KING rate code (REG.50 bits 2-3). */
#define PCFV_ADPCM_RATE0_HZ 31468u
#define PCFV_ADPCM_RATE1_HZ 15734u
#define PCFV_ADPCM_RATE2_HZ 7867u
#define PCFV_ADPCM_RATE3_HZ 3934u
#define PCFV_ADPCM_RATE0_SAMPLES_PER_FIELD 524u

#endif
