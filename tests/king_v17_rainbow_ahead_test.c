/* Grant-time read-ahead and three-word post-block guard per C6272_2 §3.4.2. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"
void PCFXIRQ_Assert(int source, bool value) {(void)source; (void)value;}
void SoundBox_ADPCMReceiveWord(int ch, uint16 value) {(void)ch; (void)value;}
void SoundBox_SetKINGADPCMControl(uint32 value) {(void)value;}
static king_t test;
int main(void)
{
 king=&test; king->KRAM_Mode=1;
 HPhase=HPHASE_HBLANK_PART1; HPhaseCounter=48; fx_vce.in_hblank=1;
 king->RAINBOWKRAMReadPos=0x40u*2;
 king->KRAM[0][0x40]=0x1122;
 king->KRAM[0][0x41]=0x3344;
 king->KRAM[0][0x42]=0x5566;
 KING_RainbowAheadSchedule(100);
 assert(king->RainbowAheadStatus[0]==1);
 assert(king->RainbowAheadGrantAt[0]==104);
 assert(king->RainbowAheadGrantAt[1]==108);
 assert(king->RainbowAheadGrantAt[2]==112);
 KING_KBusReserve(0,KBUS_REFRESH,100);
 assert(king->RainbowAheadGrantAt[0]==116); /* displaced by refresh */
 KING_RainbowAheadCommit(104);
 assert(king->RainbowAheadStatus[0]==1);
 KING_RainbowAheadCommit(116);
 assert(king->RainbowAheadStatus[0]==2);
 assert(king->RainbowAheadValue[0]==0x1122);
 king->KRAM[0][0x40]=0xAABB; /* CPU write after read grant */
 king->KRAM[0][0x41]=0xCCDD;
 king->KRAM[0][0x42]=0xEEFF;
 assert(KING_RB_Fetch()==0x22);
 assert(KING_RB_Fetch()==0x11);
 assert(king->RainbowAheadStatus[0]==0);
 assert(KING_RB_Fetch()==0x44);
 assert(KING_RB_Fetch()==0x33);
 assert(KING_RB_Fetch()==0x66);
 assert(KING_RB_Fetch()==0x55);
 assert(king->RainbowAheadStatus[2]==0);
 assert(king->RAINBOWKRAMReadPos==0x86u);
 /* A later source-address reset discards even a pending prefetch. */
 king->RAINBOWKRAMReadPos=0x90u*2;
 KING_RainbowAheadSchedule(200);
 assert(king->RainbowAheadStatus[0]==1);
 KING_RainbowAheadDiscard();
 assert(king->RainbowAheadStatus[0]==0);
 /* HBlank priority is RAINBOW > SCSI: the compressed word gets the
  * pre-DMA value, and the SCSI DMA commit follows after displacement. */
 king->KRAM[0][0x40]=0x2468;
 king->RAINBOWKRAMReadPos=0x80;
 assert(KING_QueueSCSIWord(0,0x40,0xBEEF,300));
 KING_RainbowAheadSchedule(300);
 assert(king->RainbowAheadGrantAt[0]==304);
 assert(king->SCSIPendingGrantAt[0]>304);
 KING_RainbowAheadCommit(304);
 KING_CommitPendingSCSI(304);
 assert(king->RainbowAheadValue[0]==0x2468 && king->KRAM[0][0x40]==0x2468);
 KING_CommitPendingSCSI(king->SCSIPendingGrantAt[0]);
 assert(king->KRAM[0][0x40]==0xBEEF);
 assert(KING_RB_Fetch()==0x68);
 assert(KING_RB_Fetch()==0x24);
 puts("PASS v17: three granted post-block words, refresh displacement, grant-time snapshot and source reset");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
