/* HuC6271 grant-fed JPEG coefficient progress checker.
 * The 8/9/12-bit Huffman lookups mirror rainbow_accurate.c. No synthetic
 * input on starvation: the state only advances for complete Huffman tokens.
 * This is an entropy-side decoder, NOT the display/IDCT backend. */
#ifndef PCFX_RAINBOW_HUFFMAN_STREAM_H
#define PCFX_RAINBOW_HUFFMAN_STREAM_H
#include <stdint.h>
#include <string.h>

typedef struct { uint8_t val, bitc; } RBH_LutPair;
static const RBH_LutPair RBH_dcy[512] = {
#include "rainbow_dcy.inc"
};
static const RBH_LutPair RBH_dcu[256] = {
#include "rainbow_dcuv.inc"
};
static const RBH_LutPair RBH_acy[4096] = {
#include "rainbow_acy.inc"
};
static const RBH_LutPair RBH_acu[4096] = {
#include "rainbow_acuv.inc"
};

static const uint8_t RBH_zigzag[63] = {
 0x01,0x08,0x10,0x09,0x02,0x03,0x0a,0x11,
 0x18,0x20,0x19,0x12,0x0b,0x04,0x05,0x0c,
 0x13,0x1a,0x21,0x28,0x30,0x29,0x22,0x1b,
 0x14,0x0d,0x06,0x07,0x0e,0x15,0x1c,0x23,
 0x2a,0x31,0x38,0x39,0x32,0x2b,0x24,0x1d,
 0x16,0x0f,0x17,0x1e,0x25,0x2c,0x33,0x3a,
 0x3b,0x34,0x2d,0x26,0x1f,0x27,0x2e,0x35,
 0x3c,0x3d,0x36,0x2f,0x37,0x3e,0x3f
};
typedef struct {
 uint32_t cursor, end, reservoir;
 uint32_t completed_symbols, completed_components, completed_columns, stalls;
 int32_t dc[3];
 /* Raw signed coefficients for the current 16x16 MCU. They are committed
  * only when their Huffman token and amplitude have both arrived. */
 int16_t coeff[6][64];
 uint32_t coefficient_digest;
 uint8_t bits, column, component, phase, ac_index, quant_scale;
 uint8_t active, finished, invalid, reserved;
} RBH_State;

static void RBH_Start(RBH_State *s, uint32_t cursor, uint32_t end)
{
 memset(s,0,sizeof(*s));
 s->cursor=cursor; s->end=end; s->active=1; s->quant_scale=4;
 s->coefficient_digest=2166136261u;
}

/* Work with a trial copy. FF stuffing must arrive together; only actual
 * granted bytes are loaded and no zero padding is added to saved state. */
static int RBH_Peek(RBH_State *trial, const uint8_t *src, uint32_t available,
                    unsigned width, uint32_t *value, unsigned *known)
{
 while(trial->bits<width && trial->cursor<trial->end)
 {
  if(trial->cursor>=available) break;
  const uint8_t byte=src[trial->cursor];
  if(byte==0xff && trial->cursor+1<trial->end)
  {
   if(trial->cursor+1>=available) break;
   trial->cursor+=2;
  }
  else trial->cursor++;
  trial->reservoir=(trial->reservoir<<8)|byte;
  trial->bits+=8;
 }
 *known=trial->bits;
 if(!*known) return 0;
 *value=trial->bits>=width
        ? ((trial->reservoir>>(trial->bits-width))&((1u<<width)-1u))
        : ((trial->reservoir<<(width-trial->bits))&((1u<<width)-1u));
 return 1;
}
static void RBH_Drop(RBH_State *s, unsigned n)
{
 s->bits-=n;
 s->reservoir &= s->bits ? ((1u<<s->bits)-1u) : 0u;
}
/* -1: no complete symbol/value yet; 0: success; +1: invalid lookup. */
static int RBH_Read(RBH_State *s, const uint8_t *src, uint32_t available,
                    const RBH_LutPair *lut, unsigned width, int dc_mode, int *code, int *delta)
{
 RBH_State trial=*s;
 uint32_t index=0, val=0; unsigned known=0;
 if(!RBH_Peek(&trial,src,available,width,&index,&known)) return -1;
 const RBH_LutPair p=lut[index];
 if(!p.bitc || p.bitc>width) return 1;
 if(known<p.bitc) return -1;
 RBH_Drop(&trial,p.bitc);
 const unsigned extra=(dc_mode && p.val>=15) ? 0 : (p.val&15);
 if(extra)
 {
  if(!RBH_Peek(&trial,src,available,extra,&val,&known) || known<extra) return -1;
  RBH_Drop(&trial,extra);
  *delta=(val<(1u<<(extra-1))) ? (int)val+1-(1<<extra) : (int)val;
 }
 else *delta=0;
 *code=p.val;
 *s=trial;
 return 0;
}
typedef void (*RBH_ColumnCallback)(void *opaque, const RBH_State *state, unsigned column);

static void RBH_NextComponent(RBH_State *s, RBH_ColumnCallback cb, void *opaque)
{
 /* Stable checksum for testing grant-by-grant coefficient equivalence.
  * Do not confuse this with quantized/IDCT pixel output. */
 for(unsigned i=0;i<64;i++)
 {
  const uint16_t c=(uint16_t)s->coeff[s->component][i];
  s->coefficient_digest=(s->coefficient_digest^(c&255u))*16777619u;
  s->coefficient_digest=(s->coefficient_digest^(c>>8))*16777619u;
 }
 s->completed_components++;
 s->component++; s->phase=0; s->ac_index=0;
 if(s->component==6)
 {
  if(cb) cb(opaque,s,s->column);
  memset(s->coeff,0,sizeof(s->coeff));
  s->component=0; s->column++; s->completed_columns++;
  if(s->column>=16) { s->finished=1; s->active=0; }
 }
}
/* At most max_tokens per call. Progress is transactional at the level of
 * individual Huffman symbol+additional-bits, so a coefficient never consumes
 * its prefix before its amplitude arrives. */
static void RBH_AdvanceColumns(RBH_State *s, const uint8_t *src,
                               uint32_t available, unsigned max_tokens,
                               RBH_ColumnCallback callback, void *opaque)
{
 if(!s->active || s->finished || s->invalid) return;
 if(available>s->end) available=s->end;
 for(unsigned step=0; step<max_tokens && s->active; step++)
 {
  const RBH_LutPair *lut;
  unsigned width;
  if(s->phase==0)
  {
   lut=(s->component<4)?RBH_dcy:RBH_dcu;
   width=(s->component<4)?9:8;
  }
  else
  {
   lut=(s->component<4)?RBH_acy:RBH_acu;
   width=12;
  }
  int code=0, delta=0;
  /* The luma DC 0xF special invokes an AC token and can skip columns.
   * It is distinct from the ordinary DC extra-value path. */
  RBH_State checkpoint=*s;
  int status=RBH_Read(s,src,available,lut,width,s->phase==0,&code,&delta);
  if(status<0) { s->stalls++; return; }
  if(status>0) { s->invalid=1; s->active=0; return; }
  s->completed_symbols++;
  if(s->phase==0)
  {
   if(s->component<4 && code>=0x10)
   {
    s->quant_scale=(uint8_t)((code-0x10)&15);
    continue;
   }
   if(s->component<4 && code==0x0f)
   {
    int ac=0, ignored=0;
    status=RBH_Read(s,src,available,RBH_acy,12,0,&ac,&ignored);
    if(status<0) { *s=checkpoint; s->stalls++; return; }
    if(status>0) { s->invalid=1; s->active=0; return; }
    s->completed_symbols++;
    if(s->component==0)
    {
     unsigned skip=(unsigned)(ac>>4)+1u;
     if(skip>16u-s->column) skip=16-s->column;
     s->column+=(uint8_t)skip;
     s->completed_columns+=skip;
     memset(s->dc,0,sizeof(s->dc));
     if(s->column>=16) { s->finished=1; s->active=0; }
     continue;
    }
    /* In columns' subsequent luma blocks zero-run is ignored. */
    delta=0;
   }
   const unsigned dci=(s->component<4)?0:s->component-3;
   s->dc[dci]+=delta;
   s->coeff[s->component][0]=(int16_t)s->dc[dci];
   s->phase=1; s->ac_index=0;
  }
  else
  {
   const unsigned extra=code&15;
   unsigned zeroes=(unsigned)(code>>4);
   if(extra==0 && zeroes==0) { RBH_NextComponent(s,callback,opaque); continue; }
   if(extra==0 && zeroes==1) zeroes=15;
   if(s->ac_index+zeroes<63)
    s->coeff[s->component][RBH_zigzag[s->ac_index+zeroes]]=(int16_t)delta;
   unsigned consume=zeroes+1;
   if(consume>63u-s->ac_index) consume=63-s->ac_index;
   s->ac_index+=(uint8_t)consume;
   if(s->ac_index>=63) RBH_NextComponent(s,callback,opaque);
  }
 }
}
/* The original HuC6271 decoder can read trailing Y-DC quantizer-rescale
 * opcodes *after* column 15.  These apply to the NEXT F8 strip, even though
 * the current MCU stream has already finished producing pixels.  Never
 * synthesize missing bits: all lookups are transactional on granted bytes. */
static void RBH_DrainTrailingRescales(RBH_State *s,const uint8_t *src,uint32_t available)
{
 if(!s->finished || s->invalid) return;
 for(unsigned i=0;i<256;i++)
 {
  RBH_State trial=*s;
  uint32_t idx=0; unsigned known=0;
  if(!RBH_Peek(&trial,src,available,9,&idx,&known)) return;
  const RBH_LutPair p=RBH_dcy[idx];
  if(!p.bitc || known<p.bitc || p.val<0x10 || p.val>0x1f) return;
  RBH_Drop(&trial,p.bitc);
  trial.quant_scale=(uint8_t)(p.val-0x10);
  trial.completed_symbols++;
  *s=trial;
 }
}

/* Backwards-compatible parser for users that do not consume the columns. */
static void RBH_Advance(RBH_State *s, const uint8_t *src,
                        uint32_t available, unsigned max_tokens)
{
 RBH_AdvanceColumns(s,src,available,max_tokens,NULL,NULL);
}
#endif
