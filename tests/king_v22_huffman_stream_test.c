#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../mednafen/pcfx/rainbow_huffman_stream.h"

static uint8_t packed[512];
static unsigned used_bits;
static void emit(unsigned value, unsigned width)
{
 for(unsigned i=0;i<width;i++)
 {
  const unsigned bit=(value>>(width-1-i))&1u;
  packed[used_bits/8] |= bit<<(7-(used_bits%8));
  used_bits++;
 }
}
static void code_for(const RBH_LutPair *t,unsigned width,unsigned wanted)
{
 for(unsigned i=0;i<(1u<<width);i++) if(t[i].val==wanted && t[i].bitc)
 {
  const unsigned bits=t[i].bitc;
  emit(i>>(width-bits),bits);
  return;
 }
 assert(!"missing Huffman code");
}
static void make_empty_columns(int nonzero)
{
 for(unsigned col=0;col<16;col++)
  for(unsigned c=0;c<6;c++)
  {
   if(nonzero && col==0 && c==0)
   {
    code_for(RBH_dcy,9,1); emit(1,1);          // +1 DC differential
    code_for(RBH_acy,12,1); emit(1,1);         // +1 AC at zigzag[0]
   }
   else code_for(c<4?RBH_dcy:RBH_dcu,c<4?9:8,0);
   code_for(c<4?RBH_acy:RBH_acu,12,0);        // EOB
  }
}
int main(void)
{
 make_empty_columns(0);
 const unsigned bytes=(used_bits+7)/8;
 uint8_t input[512]={0};
 memcpy(input,packed,bytes);
 RBH_State st;
 RBH_Start(&st,0,bytes);
 /* Every source byte arrives on a separate grant. The prefix and its
  * amplitude must be transacted together. */
 for(unsigned avail=1;avail<=bytes;avail++)
  RBH_Advance(&st,input,avail,1024);
 assert(st.finished && !st.invalid);
 assert(st.completed_columns==16 && st.completed_components==96);
 assert(st.stalls>0);
 /* Save-state-like memcpy roundtrip mid-symbol. */
 RBH_State a,b;
 RBH_Start(&a,0,bytes);
 RBH_Advance(&a,input,1,512);
 memcpy(&b,&a,sizeof(a));
 for(unsigned avail=2;avail<=bytes;avail++)
  RBH_Advance(&a,input,avail,1024), RBH_Advance(&b,input,avail,1024);
 assert(!memcmp(&a,&b,sizeof(a)) && a.finished);
 /* A split stuffed FF may not advance the input cursor on its own. */
 uint8_t stuff[]={0xff,0x00,0x80};
 RBH_Start(&a,0,sizeof(stuff));
 RBH_Advance(&a,stuff,1,64);
 assert(a.cursor==0);
 RBH_State trial=a;
 uint32_t raw=0; unsigned known=0;
 assert(RBH_Peek(&trial,stuff,2,12,&raw,&known));
 assert(trial.cursor==2 && known==8); // FF+stuffing arrived atomically
 const uint32_t zero_digest=st.coefficient_digest;
 memset(packed,0,sizeof(packed)); used_bits=0;
 make_empty_columns(1);
 memset(input,0,sizeof(input));
 const unsigned nbytes=(used_bits+7)/8;
 memcpy(input,packed,nbytes);
 RBH_Start(&st,0,nbytes);
 for(unsigned avail=1;avail<=nbytes;avail++) RBH_Advance(&st,input,avail,1024);
 assert(st.finished && st.completed_columns==16);
 assert(st.coefficient_digest!=zero_digest);
 printf("PASS v22: %u zero-MCU bits, 96 coefficients, bytewise grant stalls, state roundtrip, FF stuffing\n", used_bits);
 return 0;
}
