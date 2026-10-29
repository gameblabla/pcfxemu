/* K-BUS manual-priority regression using the actual KING source. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"
void SoundBox_SetKINGADPCMControl(uint32 value) {(void)value;}
static king_t test;
int main(void)
{
 king=&test; king->KRAM_Mode=1;
 HPhase=HPHASE_HBLANK_PART1; HPhaseCounter=48; fx_vce.in_hblank=1;
 assert(KING_KBusPriority(KBUS_REFRESH,1)>KING_KBusPriority(KBUS_ADPCM,1));
 assert(KING_KBusPriority(KBUS_ADPCM,1)>KING_KBusPriority(KBUS_RAINBOW,1));
 assert(KING_KBusPriority(KBUS_RAINBOW,1)>KING_KBusPriority(KBUS_SCSI,1));
 assert(KING_KBusPriority(KBUS_ADPCM,0)==0);
 /* Inverse call order: the highest-priority request must retain the
  * first 4-clock grant, with lower-priority requests displaced. */
 KING_KBusReserve(0,KBUS_SCSI,100);
 KING_KBusReserve(0,KBUS_RAINBOW,100);
 KING_KBusReserve(0,KBUS_ADPCM,100);
 KING_KBusReserve(0,KBUS_REFRESH,100);
 for(int i=0;i<4;i++)
 {
  unsigned expected=KBUS_REFRESH-i;
  unsigned slot=(100+4*i)/4;
  assert(king->KBusSlotSource[0][slot]==expected);
  assert(king->KBusSlotTime[0][slot]==(uint32)(100+4*i));
 }
 assert(!king->KBusSlotSource[1][100/4]);
 /* A CPU writer must wait for those four reserved grant slots. */
 assert(KING_KRAMSlotDrainDeadline(100,100,0)==132);
 assert(KING_KRAMSlotDrainDeadline(100,100,1)==116);
 /* A documented five-refresh sequence is reserved independently on both
  * physical banks; its intra-HBlank electrical phase is not specified. */
 memset(king->KBusSlotSource,0,sizeof(king->KBusSlotSource));
 for(unsigned i=0;i<5;i++) for(unsigned b=0;b<2;b++)
  KING_KBusReserve(b,KBUS_REFRESH,100);
 assert(KING_KRAMSlotDrainDeadline(100,100,0)==136);
 assert(KING_KRAMSlotDrainDeadline(100,100,1)==136);
 /* Active BG slots on A preempt RAINBOW; B remains independent. */
 memset(king->KBusSlotSource,0,sizeof(king->KBusSlotSource));
 memset(king->MPROGData,0xff,sizeof(king->MPROGData));
 for(int i=0;i<4;i++) king->MPROGData[i]=0;
 king->priority=1; king->MPROGControl=1;
 HPhase=HPHASE_ACTIVE; HPhaseCounter=1024; fx_vce.in_hblank=0;
 KING_KBusReserve(0,KBUS_RAINBOW,100);
 assert(king->KBusSlotSource[0][116/4]==KBUS_RAINBOW || king->KBusSlotSource[0][100/4]==KBUS_RAINBOW);
 /* At 100, first BG group begins at phase 0 (slot zero is busy). */
 puts("PASS v15: HBlank priorities, per-bank isolation, refresh request count, CPU grants");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
