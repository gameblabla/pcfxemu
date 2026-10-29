/* Diagnostic incremental MCU luma reconstruction, v23.
 * This is deliberately not the displayed RAINBOW output. The existing
 * renderer remains authoritative until coefficient parity is established.
 * No hardware execution latency is inferred from this software IDCT. */
#ifndef PCFX_RAINBOW_MCU_LUMA_H
#define PCFX_RAINBOW_MCU_LUMA_H
#include "rainbow_huffman_stream.h"
#include "idct.h"
#include "jrevdct.h"

static inline unsigned RBM_Clamp(int x)
{
 return x<0?0:(x>255?255:(unsigned)x);
}
/* One completed 16x16 column; output arranged in row-major 16x16 pixels.
 * Quantization follows RescaleQuantTables() in rainbow_accurate.c.
 * The parser's current scale is used for all luma blocks in the MCU;
 * changing the scale within an MCU is marked for later verification. */
static void RBM_ReconstructLuma(const RBH_State *s, const uint8_t qbase[64],
                                uint8_t out[256], int use_fast_idct)
{
 int32 blocks[4][64];
 const unsigned scale=s->quant_scale;
 for(unsigned c=0;c<4;c++)
 {
  for(unsigned i=0;i<64;i++)
  {
   unsigned q=((unsigned)qbase[i]*scale)>>2;
   q=q<1?1:(q>254?254:q);
   blocks[c][i]=(int16_t)(q*(int32)s->coeff[c][i]);
  }
  if(use_fast_idct) j_rev_dct(blocks[c]);
  else IDCT(blocks[c]);
 }
 for(unsigned y=0;y<16;y++)
  for(unsigned x=0;x<16;x++)
  {
   const unsigned c=((x&8)?2:0)+((y&8)?1:0);
   out[y*16+x]=(uint8_t)RBM_Clamp(blocks[c][(y&7)*8+(x&7)]+128);
  }
}
#endif
