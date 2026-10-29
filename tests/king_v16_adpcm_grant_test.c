/* ADPCM K-BUS request/retirement; use real KING functions. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"
void PCFXIRQ_Assert(int source, bool value) {(void)source; (void)value;}
static king_t test;
static unsigned deliveries[2];
static uint16 received[2];
void SoundBox_ADPCMReceiveWord(int ch, uint16 word) { deliveries[ch]++; received[ch]=word; }
void SoundBox_SetKINGADPCMControl(uint32 value) {(void)value;}
int main(void)
{
 king=&test; king->KRAM_Mode=1;
 king->ADPCMControl=3;
 king->ADPCMEndAddress[0]=0x1ffff;
 king->ADPCMEndAddress[1]=0x1ffff;
 HPhase=HPHASE_HBLANK_PART1;HPhaseCounter=48;fx_vce.in_hblank=1;
 king_gfx_ts=100;
 king->KRAM[0][0x10]=0x1234;
 king->ADPCMPlayAddress[0]=0x10;
 KING_RequestADPCMHalfWord(0);
 assert(king->ADPCMGrantPending[0] && deliveries[0]==0);
 assert(king->ADPCMPlayAddress[0]==0x11);
 assert(king->ADPCMGrantAt[0]==104);
 KING_KBusReserve(0,KBUS_REFRESH,100);
 assert(king->ADPCMGrantAt[0]==108);
 KING_CommitPendingADPCM(104);
 assert(deliveries[0]==0);
 KING_CommitPendingADPCM(108);
 assert(deliveries[0]==1 && received[0]==0x1234);
 /* Explicit R50 disable invalidates a pending capture. */
 king_gfx_ts=200;
 king->KRAM[0][0x11]=0xabcd;
 KING_RequestADPCMHalfWord(0);
 assert(king->ADPCMGrantPending[0]);
 KING_WriteADPCMTransferControl(0);
 assert(!king->ADPCMGrantPending[0]);
 KING_CommitPendingADPCM(500);
 assert(deliveries[0]==1);
 /* Same-bank ADPCM capture precedes a SCSI write accepted at the same
    instant; the ADPCM data must be sampled before DMA changes KRAM. */
 memset(king->KBusSlotSource,0,sizeof(king->KBusSlotSource));
 memset(king->KBusSlotTag,0,sizeof(king->KBusSlotTag));
 king->ADPCMControl=1;
 king->KRAM[0][0x30]=0x1111;
 king->ADPCMPlayAddress[0]=0x30;
 king_gfx_ts=300;
 assert(KING_QueueSCSIWord(0,0x30,0x2222,300));
 KING_RequestADPCMHalfWord(0);
 assert(king->ADPCMGrantAt[0]==304);
 assert(king->SCSIPendingGrantAt[0]==308);
 KING_CommitPendingADPCM(304);
 assert(received[0]==0x1111);
 KING_CommitPendingSCSI(304);
 assert(king->KRAM[0][0x30]==0x1111);
 KING_CommitPendingSCSI(308);
 assert(king->KRAM[0][0x30]==0x2222);
 puts("PASS v16: ADPCM HSYNC request, deferred grant, refresh displacement, disable cancellation");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
