/* KING optional posted-write KRAM visibility: no actual hardware required. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"
void SoundBox_SetKINGADPCMControl(uint32 value) {(void)value;}
static king_t test;
int main(void)
{
 king = &test;
 king->AR = 0x0E;
 king->KRAM_Mode = 1;
 king->KRAM[0][0x20000] = 0x1234;
 king->KRAMWritePending = 1;
 king->KRAMWritePendingPage = 0;
 king->KRAMWritePendingAddress = 0x20000;
 king->KRAMWritePendingValue = 0xBEEF;
 king->KRAMWritePendingAt = 116;
 king->KRAMWriteBusyUntil = 200; /* CPU can reserve the next write first! */
 KING_CommitPendingKRAM(115);
 assert(test.KRAM[0][0x20000] == 0x1234);
 assert(king->KRAMWritePending);
 KING_CommitPendingKRAM(116);
 assert(test.KRAM[0][0x20000] == 0xBEEF);
 assert(!king->KRAMWritePending);
 assert(king->KRAMWriteBusyUntil == 200);
 /* A pending B write must never be visible to A before retirement. */
 king->KRAM[0][33] = 0x2222;
 king->KRAM[1][33] = 0x3333;
 king->KRAMWritePending = 1;
 king->KRAMWritePendingPage = 1;
 king->KRAMWritePendingAddress = 33;
 king->KRAMWritePendingValue = 0xFACE;
 king->KRAMWritePendingAt = 230;
 KING_CommitPendingKRAM(229);
 assert(king->KRAM[1][33] == 0x3333);
 KING_CommitPendingKRAM(230);
 assert(king->KRAM[0][33] == 0x2222 && king->KRAM[1][33] == 0xFACE);
 /* The slot scheduler must route the bank using bit 31, not bit 17. */
 king->MPROGControl = 1;
 king->priority = 1;
 memset(king->MPROGData, 0xff, sizeof(king->MPROGData));
 for(int i=0;i<4;i++) king->MPROGData[i]=0;
 HPhase=HPHASE_ACTIVE;
 HPhaseCounter=1024;
 fx_vce.in_hblank=0;
 assert(KING_KRAMSlotDrainDeadline(100,100,0) == 132);
 assert(KING_KRAMSlotDrainDeadline(100,100,1) == 116);
 puts("PASS v13: retirement visibility, prior-write timestamp, bank isolation");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
