/* Unit tests for documented DAC input levels and analog-YUV RGB presentation.
 * No claim about unmeasured PCB, encoder or CRT transfer functions. */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "mednafen/pcfx/pcfx_yuv_color.h"
static unsigned clamp_rgb(int x) {return x < 0 ? 0 : x > 255 ? 255 : (unsigned)x;}
static void check(unsigned y, unsigned u, unsigned v,
                  unsigned r, unsigned g, unsigned b)
{
 PCFX_YUVColorOffsets o=PCFX_AnalogYUVOffsets(u,v);
 assert(clamp_rgb((int)y+o.r)==r);
 assert(clamp_rgb((int)y+o.g)==g);
 assert(clamp_rgb((int)y+o.b)==b);
}
int main(void)
{
 /* Center chroma always produces grayscale across the entire Y range. */
 for(unsigned y=0;y<256;y++) check(y,128,128,y,y,y);
 assert(PCFX_YUVPalettePacked(0x8088)==0x808080);
 assert(PCFX_YUVPalettePacked(0xFF0F)==0xFF00F0);
 assert(PCFX_YUVPalettePacked(0x4070)==0x407000);
 /* A positive U mostly drives blue; a positive V drives red. */
 PCFX_YUVColorOffsets blue=PCFX_AnalogYUVOffsets(240,128);
 PCFX_YUVColorOffsets red=PCFX_AnalogYUVOffsets(128,240);
 assert(blue.b>0 && blue.g<0 && blue.r==0);
 assert(red.r>0 && red.g<0 && red.b==0);
 assert(blue.b > red.r);
 /* RGB is converted at final presentation, not separately per plane. */
 unsigned yuv = PCFX_YUVPalettePacked(0x8088);
 PCFX_YUVColorOffsets gray=PCFX_AnalogYUVOffsets((yuv>>8)&255,yuv&255);
 assert(gray.r==0 && gray.g==0 && gray.b==0);
 puts("PASS: HuC6261 YUV color conversion (neutral, gamut, palette packing)");
}
