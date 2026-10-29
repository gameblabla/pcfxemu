/* Asynchronous, grant-fed HuC6271 block input, HBlank priority and FIFO test. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"
void PCFXIRQ_Assert(int source, bool value) {(void)source;(void)value;}
void SoundBox_ADPCMReceiveWord(int ch, uint16 value) {(void)ch;(void)value;}
void SoundBox_SetKINGADPCMControl(uint32 value) {(void)value;}
static unsigned decodes, bytecount;
void RAINBOW_DecodeBlock(bool first, bool skip)
{
 (void)first;(void)skip;
 decodes++;
 while(king->RBAsyncConsumeIndex<6u)
 {
  (void)KING_RB_Fetch();
  bytecount++;
 }
}
static king_t test;
int main(void)
{
 king=&test; king->KRAM_Mode=1;
 HPhase=HPHASE_HBLANK_PART1; HPhaseCounter=256; fx_vce.in_hblank=1;
 king->RAINBOWKRAMReadPos=0x80;
 king->RBAsyncProducerPos=0x80;
 /* 0xFF F0: palettized block, length=4, two literal bytes. */
 king->KRAM[0][0x40]=0xF0FF;
 king->KRAM[0][0x41]=0x0400;
 king->KRAM[0][0x42]=0x2211;
 /* Two inner dummy bytes and three zero guard words are captured too. */
 for(unsigned a=0x43;a<=0x46;a++) king->KRAM[0][a]=0;
 KING_RainbowAsyncStart(true,false,100);
 assert(king->RBAsyncPending && king->RBAsyncGrantAt==104);
 KING_KBusReserve(0,KBUS_REFRESH,100);
 assert(king->RBAsyncGrantAt>104);
 KING_RainbowAsyncCommit(104);
 assert(king->RBAsyncLength==0);
 const uint32 done=king->RBAsyncGrantAt;
 KING_RainbowAsyncCommit(done);
 assert(king->RBAsyncStage[0]==0xff && king->RBAsyncStage[1]==0xf0);
 assert(king->RBAsyncLength==2 && king->RBAsyncScanState==2);
 king->KRAM[0][0x40]=0xBEEF;
 /* Finish the captured block. No decoder byte is fetched from live KRAM. */
 for(unsigned i=0;i<25 && !decodes;i++)
 {
  assert(king->RBAsyncPending);
  KING_RainbowAsyncCommit(king->RBAsyncGrantAt);
 }
 assert(decodes==1 && bytecount==6);
 assert(king->RBAsyncUnderflows==0 && !king->RBAsyncActive);
 assert(king->RAINBOWKRAMReadPos==0x86);
 assert(king->RBAsyncWords==7 && king->RBAsyncLength==8);
 KING_RainbowAheadDiscard();
 puts("PASS v19: async grants, refresh displacement, captured compressed bytes and complete-block gating");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
