/* HuC6272 §3.3.4: byte count and SCSI ACK backpressure. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"
void SoundBox_SetKINGADPCMControl(uint32 value) {(void)value;}
void PCFXIRQ_Assert(int source, bool value) {(void)source; (void)value;}
static king_t test;
int main(void)
{
 king=&test; king->KRAM_Mode=1;
 HPhase=HPHASE_HBLANK_PART1; HPhaseCounter=48; fx_vce.in_hblank=1;
 king->DMATransferAddr=0x100; king->DMATransferSize=4;
 king->DMAStatus=1; king->DMATransferFlipFlop=0;
 king->KRAM[0][0x100]=0xFFFF;
 kbus_event_ts=100;
 assert(DoRealDMA(0x12));
 assert(king->DMATransferSize==3); /* per-byte count */
 assert(king->DMATransferFlipFlop==1);
 /* Saturate the model queue. This is not a hardware FIFO depth claim. */
 for(unsigned n=0;n<KING_SCSI_PENDING_MAX;n++)
 {
  king->SCSIPendingValid[n]=1;
  king->SCSIPendingGrantAt[n]=1000000;
 }
 king->SCSIPendingCount=KING_SCSI_PENDING_MAX;
 assert(!DoRealDMA(0x34));
 assert(king->DMATransferSize==3);
 assert(king->DMATransferAddr==0x100);
 assert(king->DMATransferFlipFlop==1);
 assert(king->DMALatch==0x12);
 assert(king->KRAM[0][0x100]==0xFFFF); /* no eager commit */
 /* A completed transaction frees capacity; the SAME byte can retry. */
 king->SCSIPendingValid[0]=0;
 king->SCSIPendingCount--;
 assert(DoRealDMA(0x34));
 assert(king->DMATransferSize==2);
 assert(king->DMATransferAddr==0x101);
 assert(king->DMATransferFlipFlop==0);
 assert(king->KRAM[0][0x100]==0xFFFF);
 KING_CommitPendingSCSI(104);
 assert(king->KRAM[0][0x100]==0x3412);
 /* The second complete word decrements both bytes, even on DMA end. */
 kbus_event_ts=108;
 assert(DoRealDMA(0x56));
 assert(king->DMATransferSize==1);
 assert(DoRealDMA(0x78));
 assert(king->DMATransferSize==0);
 assert(!(king->DMAStatus&1));
 puts("PASS v18 SCSI: byte counter, no ACK when pending queue full, no eager data visibility");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
