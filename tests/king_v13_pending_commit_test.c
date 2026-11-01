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
 setenv("PCFXEMU_KRAM_MAPPED_OLD","1",1);   /* the mapped-window 4-grant model is retired (DIAG9 K11/K12 measured ST = OUT) but selectable */
 setenv("PCFXEMU_KRAM_BG_TAIL","0",1);            /* these pin the slot scheduler itself, without the BG-fetch tail fitted to K2 n=8 */
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
 /* The slot scheduler routes the microprogram half by KRAM address bit 17 (DIAG7 K2: a bank-A microprogram never slows
  * writes to bank B).  `bank` is the K-BUS page, `mbank` the address bit. */
 king_kram_write_mapped = 1;		/* mapped path: the original 4-grant model */
 king->MPROGControl = 1;
 king->priority = 1;
 memset(king->MPROGData, 0xff, sizeof(king->MPROGData));
 for(int i=0;i<4;i++) king->MPROGData[i]=0;
 HPhase=HPHASE_ACTIVE;
 HPhaseCounter=1024;
 fx_vce.in_hblank=0;
 fx_vce.raster_counter=100;
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 132);
 assert(KING_KRAMSlotDrainDeadline(100,100,0,1) == 116);
 king_kram_write_mapped = 0;		/* OUT.H path: one grant + 8 clocks of recovery */
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 128);
 assert(KING_KRAMSlotDrainDeadline(100,100,0,1) == 112);
 puts("PASS v13: retirement visibility, prior-write timestamp, bank isolation");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
