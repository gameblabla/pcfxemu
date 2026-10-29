/* v23: coefficient-completion to IDCT: independent, no display side effects. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/rainbow_mcu_luma.h"
static unsigned callbacks;
static void completed(void *opaque,const RBH_State *s,unsigned col)
{
 unsigned *last=(unsigned*)opaque;
 callbacks++;
 assert(col==*last);
 assert(s->component==6);
 assert(s->coeff[0][0]==80);
 assert(s->coeff[5][0]==12);
}
int main(void)
{
 RBH_State state;
 RBH_Start(&state,0,0);
 uint8 q[64],y[256];
 memset(q,4,sizeof(q));
 RBM_ReconstructLuma(&state,q,y,0);
 for(unsigned i=0;i<256;i++) assert(y[i]==128);
 state.coeff[0][0]=80;
 state.coeff[5][0]=12;
 RBM_ReconstructLuma(&state,q,y,0);
 int32 raw[64]={0};raw[0]=320;
 IDCT(raw);
 assert(y[0]==RBM_Clamp(raw[0]+128));
 assert(y[7]==RBM_Clamp(raw[7]+128));
 assert(y[8]==128);  /* top-right luma is a different component */
 state.component=5;
 unsigned col=0;
 RBH_NextComponent(&state,completed,&col);
 assert(callbacks==1 && state.column==1 && state.component==0);
 for(unsigned c=0;c<6;c++) for(unsigned j=0;j<64;j++) assert(state.coeff[c][j]==0);
 /* Quantizer scale zero is a *valid* rescale value and must be retained
  * across F8 strips.  It may not be interpreted as uninitialized. */
 RBH_Start(&state,0,2);
 state.finished=1;
 state.active=0;
 uint8 payload[2]={0};
 int found=0;
 for(unsigned i=0;i<512;i++)
  if(RBH_dcy[i].val==0x10 && RBH_dcy[i].bitc)
  {
   payload[0]=(uint8)(i>>1); payload[1]=(uint8)((i&1u)<<7); found=1;break;
  }
 assert(found);
 RBH_DrainTrailingRescales(&state,payload,0);
 assert(state.quant_scale==4 && state.cursor==0);
 RBH_DrainTrailingRescales(&state,payload,2);
 assert(state.quant_scale==0);
 puts("PASS v23: six-component MCU completion, IDCT luma, scale-zero, transactional trailing quantizer");
 return 0;
}
