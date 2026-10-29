#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pcfx_dotwidth.h"

static uint32_t input[4*1024], output[4*1024];

static void synth(int mode)
{
    for(int y=0; y<4; y++)
        for(int x=0; x<1024; x++)
        {
            uint32_t king = (x/4) & 1 ? 0x00DDCCBB : 0x00112233;
            uint32_t vdc  = (x/3) & 1 ? 0x00998877 : 0x00AABBCC;
            if(mode==0) input[y*1024+x] = vdc;
            if(mode==1) input[y*1024+x] = king;
            if(mode==2) input[y*1024+x] = (x/3)&1 ? vdc : king;
            if(mode==3) input[y*1024+x] = (x/12)&1 ? 0x12345678 : 0x87654321;
        }
}
int main(void)
{
    synth(0);
    assert(pcfx_dotwidth_choose(PCFX_WIDTH_AUTO, 1024, input, 1024, 4, 4)==341);
    synth(1);
    assert(pcfx_dotwidth_choose(PCFX_WIDTH_AUTO, 1024, input, 1024, 4, 4)==256);
    synth(2);
    assert(pcfx_dotwidth_choose(PCFX_WIDTH_AUTO, 1024, input, 1024, 4, 4)==1024);
    assert(pcfx_dotwidth_choose(PCFX_WIDTH_DOTS, 1024, input, 1024, 4, 4)==341);
    assert(pcfx_dotwidth_choose(PCFX_WIDTH_CLOCK, 1024, input, 1024, 4, 4)==1024);
    assert(pcfx_dotwidth_choose(PCFX_WIDTH_CLOCK, 256, input, 1024, 4, 4)==256);
    synth(3);
    assert(pcfx_dotwidth_choose(PCFX_WIDTH_AUTO, 1024, input, 1024, 4, 4)==341);
    synth(0);
    memset(output, 0, sizeof(output));
    pcfx_dotwidth_downsample(output, input, 1024, 4, 341, 4);
    for(int y=0;y<4;y++)for(int x=0;x<341;x++)
        assert(output[y*1024+x]==input[y*1024+3*x+1]);
    synth(1);
    pcfx_dotwidth_downsample(output, input, 1024, 4, 256, 4);
    for(int y=0;y<4;y++)for(int x=0;x<256;x++)
        assert(output[y*1024+x]==input[y*1024+4*x+2]);
    uint16_t pattern[2*1024];
    for(int x=0;x<1024;x++) pattern[x] = (uint16_t)(x/3);
    assert(pcfx_dotwidth_choose(PCFX_WIDTH_AUTO, 1024, pattern, 1024, 1, 2)==341);
    for(int x=0;x<1024;x++) pattern[x] = (uint16_t)(x/4);
    assert(pcfx_dotwidth_choose(PCFX_WIDTH_AUTO, 1024, pattern, 1024, 1, 2)==256);
    printf("dotwidth: auto VDC/KING/mixed, 256/341/1024, 16/32bpp, sample centres: PASS\n");
    return 0;
}
