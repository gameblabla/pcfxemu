/* Asynchronous, grant-fed HuC6271 block input, HBlank priority and FIFO test. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"
void PCFXIRQ_Assert(int source, bool value) {(void)source;(void)value;}
void SoundBox_ADPCMReceiveWord(int ch, uint16 value) {(void)ch;(void)value;}
void SoundBox_SetKINGADPCMControl(uint32 value) {(void)value;}
static unsigned decodes, bytecount, rle_starts, rle_ends, rle_runs;
static uint8 rle_image[8192];
void RAINBOW_RLEBegin(bool first) { (void)first; rle_starts++; }
void RAINBOW_RLEEmit(unsigned x, unsigned count, uint8 color)
{
 assert(x+count<=8192);
 memset(rle_image+x,color,count);
 rle_runs++;
}
void RAINBOW_RLEEnd(void) { rle_ends++; }

void RAINBOW_DecodeBlock(bool first, bool skip)
{
 (void)first;(void)skip;
 decodes++;
 while(king->RBAsyncConsumeIndex < king->RBAsyncTarget-8u)
 {
  (void)KING_RB_Fetch();
  bytecount++;
 }
}
static king_t test;
static void run_to_completion(uint32 time)
{
 KING_RainbowAsyncStart(true,false,time);
 for(unsigned i=0;i<100 && king->RBAsyncActive;i++)
 {
  assert(king->RBAsyncPending);
  KING_RainbowAsyncCommit(king->RBAsyncGrantAt);
 }
 assert(!king->RBAsyncActive);
}
int main(void)
{
 king=&test; king->KRAM_Mode=1;
 HPhase=HPHASE_HBLANK_PART1; HPhaseCounter=256; fx_vce.in_hblank=1;
 king->RAINBOWKRAMReadPos=0x80;
 king->RBAsyncProducerPos=0x80;
 /* F8 JPEG: four bytes, with the FF byte-stuffing pair split across
  * two 16-bit KRAM grants. Entropy parser must pause at that boundary. */
 const uint8 jpg[]={0xff,0xf8,0,6,0x11,0xff,0x00,0x22,0,0,0,0,0,0,0,0};
 for(unsigned i=0;i<sizeof(jpg);i+=2)
  king->KRAM[0][0x40+i/2]=(uint16)jpg[i]|((uint16)jpg[i+1]<<8);
 KING_RainbowAsyncStart(true,false,100);
 for(unsigned i=0;i<3;i++) KING_RainbowAsyncCommit(king->RBAsyncGrantAt);
 assert(king->RBJPEGActive && king->RBJPEGCursor==5);
 assert(king->RBJPEGStalledPairs>0 && king->RBJPEGEntropyBytes==1);
 while(king->RBAsyncActive) KING_RainbowAsyncCommit(king->RBAsyncGrantAt);
 assert(king->RBJPEGBlocks==1 && king->RBJPEGEntropyBytes==3);
 assert(king->RBJPEGStuffedPairs==1 && king->RBAsyncUnderflows==0);
 /* FF JPEG: 128 quant bytes must be captured separately from entropy
  * stuffing, including legitimate FF quantizer bytes. */
 memset(&test,0,sizeof(test));
 king->KRAM_Mode=1;
 HPhase=HPHASE_HBLANK_PART1; HPhaseCounter=256; fx_vce.in_hblank=1;
 king->RAINBOWKRAMReadPos=0x200;
 king->RBAsyncProducerPos=0x200;
 uint8 payload[144]={0xff,0xff,0,132};
 for(unsigned i=0;i<128;i++) payload[4+i]=(i==53)?0xff:(uint8)i;
 payload[132]=0x34; payload[133]=0x45;
 for(unsigned i=0;i<sizeof(payload);i+=2)
  king->KRAM[0][0x100+i/2]=(uint16)payload[i]|((uint16)payload[i+1]<<8);
 run_to_completion(200);
 assert(king->RBJPEGBlocks==1 && king->RBJPEGQuantCount==128);
 assert(king->RBJPEGQuant[53]==0xff && king->RBJPEGEntropyBytes==2);
 assert(king->RBJPEGStuffedPairs==0);
 puts("PASS v21: incremental JPEG quant bytes, split stuffed entropy pair, and grant-completion semantics");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
