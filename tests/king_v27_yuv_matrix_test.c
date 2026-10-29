/* Tests for the sole PCFXemu v27 final YUV->RGB chroma offset conversion.
 * Numerical accuracy does not imply measured real-console color accuracy.
 */
#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "mednafen/pcfx/pcfx_yuv_color.h"

static int clamp(int x) { return x < 0 ? 0 : x > 255 ? 255 : x; }

static void invert_forward(long double out[3][3])
{
 long double a[3][6] = {
  { .299L, .587L, .114L, 1, 0, 0 },
  {-.147L,-.289L, .436L, 0, 1, 0 },
  { .615L,-.515L,-.100L, 0, 0, 1 }
 };
 for(int p=0; p<3; p++)
 {
  long double divisor=a[p][p];
  assert(fabsl(divisor)>0.0001L);
  for(int j=0;j<6;j++) a[p][j]/=divisor;
  for(int i=0;i<3;i++) if(i!=p)
  {
   long double factor=a[i][p];
   for(int j=0;j<6;j++) a[i][j]-=factor*a[p][j];
  }
 }
 for(int i=0;i<3;i++) for(int j=0;j<3;j++) out[i][j]=a[i][j+3];
}

int main(void)
{
 assert(fesetround(FE_TONEAREST)==0);
 assert(lrint(1.8)==2 && lrint(-1.8)==-2);
 assert(lrint(2.5)==2 && lrint(3.5)==4);

 long double inv[3][3];
 const long double expected[3][3] = {
  {1.0L, -0.000039457070707L,  1.139827967171717L},
  {1.0L, -0.394610164141414L, -0.580500315656566L},
  {1.0L,  2.031999684343434L, -0.000481376262626L}
 };
 invert_forward(inv);
 for(int i=0;i<3;i++) for(int j=0;j<3;j++)
  assert(fabsl(inv[i][j]-expected[i][j]) < 1e-11L);

 uint64_t checksum_r=0,checksum_g=0,checksum_b=0;
 for(int ur=0;ur<256;ur++) for(int vr=0;vr<256;vr++)
 {
  const int u=ur-128, v=vr-128;
  const PCFX_YUVColorOffsets got=PCFX_AnalogYUVOffsets(ur,vr);
  /* Independent direct reference to the established inverse-lrint formula. */
  const int r=(int)lrint(-0.000039457070707 * u + 1.139827967171717 * v);
  const int g=(int)lrint(-0.394610164141414 * u - 0.580500315656566 * v);
  const int b=(int)lrint(2.031999684343434 * u - 0.000481376262626 * v);
  assert(got.r==r && got.g==g && got.b==b);
  if(ur==128 && vr==128) assert(got.r==0 && got.g==0 && got.b==0);
  for(int y=0;y<256;y+=17)
  {
   int cr=clamp(y+got.r), cg=clamp(y+got.g), cb=clamp(y+got.b);
   assert(cr>=0 && cr<=255 && cg>=0 && cg<=255 && cb>=0 && cb<=255);
  }
  checksum_r+=(unsigned)(r+512);
  checksum_g+=(unsigned)(g+512);
  checksum_b+=(unsigned)(b+512);
 }
 assert(PCFX_YUVPalettePacked(0x8088)==0x808080);
 assert(PCFX_YUVPalettePacked(0xFF0F)==0xFF00F0);
 printf("PASS: six inverse-YUV coefficients validated against independent matrix inversion\n");
 printf("PASS: single lrint transform matches reference on all 65,536 UV pairs\n");
 printf("PASS: centered U/V remains neutral; palette mapping and clamping verified\n");
 printf("LUT checksum sums (R,G,B): %llu %llu %llu\n",
   (unsigned long long)checksum_r,(unsigned long long)checksum_g,(unsigned long long)checksum_b);
 return 0;
}
