/* Bus-granted SCSI visibility test. Exercise the actual KING source. */
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
 king->KRAM[0][0x40]=0xaaaa;
 assert(KING_QueueSCSIWord(0,0x40,0x1234,100));
 assert(king->SCSIPendingCount==1);
 assert(king->KRAM[0][0x40]==0xaaaa);
 assert(king->SCSIPendingGrantAt[0]==104);
 KING_CommitPendingSCSI(103);
 assert(king->KRAM[0][0x40]==0xaaaa);
 /* A refresh arriving at the same model tick outranks SCSI. Its slot
    displacement MUST update the pending transfer's completion deadline. */
 KING_KBusReserve(0,KBUS_REFRESH,100);
 assert(king->SCSIPendingGrantAt[0]==108);
 KING_CommitPendingSCSI(104);
 assert(king->KRAM[0][0x40]==0xaaaa);
 KING_CommitPendingSCSI(108);
 assert(king->KRAM[0][0x40]==0x1234);
 assert(!king->SCSIPendingCount);
 /* Independent bank: A refresh on A cannot delay B. */
 memset(king->KBusSlotSource,0,sizeof(king->KBusSlotSource));
 memset(king->KBusSlotTag,0,sizeof(king->KBusSlotTag));
 king->KRAM[1][0x40]=0x5555;
 assert(KING_QueueSCSIWord(1,0x40,0xabcd,200));
 KING_KBusReserve(0,KBUS_REFRESH,200);
 assert(king->SCSIPendingGrantAt[0]==204);
 KING_CommitPendingSCSI(204);
 assert(king->KRAM[1][0x40]==0xabcd);
 /* A burst keeps word ordering and never exposes unretired words. */
 memset(king->KBusSlotSource,0,sizeof(king->KBusSlotSource));
 memset(king->KBusSlotTag,0,sizeof(king->KBusSlotTag));
 for(unsigned i=0;i<32;i++)
 {
  king->KRAM[0][0x100+i]=0xffff;
  assert(KING_QueueSCSIWord(0,0x100+i,(uint16)i,300));
 }
 assert(king->SCSIPendingCount==32);
 KING_CommitPendingSCSI(303);
 for(unsigned i=0;i<32;i++) assert(king->KRAM[0][0x100+i]==0xffff);
 KING_CommitPendingSCSI(500);
 for(unsigned i=0;i<32;i++) assert(king->KRAM[0][0x100+i]==i);
 assert(!king->SCSIPendingCount);
 /* Test the real two-byte SCSI DMA producer, not only the internal
    queue helper. Byte order is low byte first per HuC6272 §3.3.5. */
 memset(king->KBusSlotSource,0,sizeof(king->KBusSlotSource));
 memset(king->KBusSlotTag,0,sizeof(king->KBusSlotTag));
 king->KRAM[0][0x70]=0xffff;
 king->KRAM[0][0x71]=0xffff;
 king->DMATransferAddr=0x70;
 king->DMATransferSize=4;
 king->DMAStatus=1;
 king->DMATransferFlipFlop=0;
 king->DMACountBounded=0;
 kbus_event_ts=600;
 DoRealDMA(0x12);
 assert(king->SCSIPendingCount==0);
 DoRealDMA(0x34);
 assert(king->SCSIPendingCount==1);
 assert(king->KRAM[0][0x70]==0xffff);
 kbus_event_ts=604;
 DoRealDMA(0x56);
 DoRealDMA(0x78);
 assert(king->SCSIPendingCount==2);
 KING_CommitPendingSCSI(603);
 assert(king->KRAM[0][0x70]==0xffff);
 KING_CommitPendingSCSI(604);
 assert(king->KRAM[0][0x70]==0x3412);
 assert(king->KRAM[0][0x71]==0xffff);
 KING_CommitPendingSCSI(608);
 assert(king->KRAM[0][0x71]==0x7856);
 puts("PASS v16: deferred SCSI word commit, priority displacement, banks, burst ordering");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
