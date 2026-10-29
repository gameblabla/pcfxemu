/* Opt-in KRAM grant scheduler invariants. Run against the actual KING source. */
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
 king->AR = 0x0e;
 king->priority = 1;
 king->MPROGControl = 1;
 memset(king->MPROGData, 0xff, sizeof(king->MPROGData));
 HPhase = HPHASE_ACTIVE;
 HPhaseCounter = 1024;
 fx_vce.in_hblank = 0;
 /* Four dot grants at 4 clocks each, no other clients. */
 assert(KING_KRAMSlotDrainDeadline(100,100,0) == 116);
 assert(KING_KRAMSlotDrainDeadline(100,100,1) == 116);
 /* Bank A: four BG requests, then four slots available for CPU. */
 for(int i=0;i<4;i++) king->MPROGData[i] = 0;
 assert(KING_KRAMSlotDrainDeadline(100,100,0) == 132);
 /* Bank B is independent; unoccupied A slots do not burden B. */
 assert(KING_KRAMSlotDrainDeadline(100,100,1) == 116);
 /* Putting ALU work between writes hides the latch drain. */
 HPhaseCounter = 988;
 assert(KING_KRAMSlotDrainDeadline(136,136,0) == 164);
 HPhaseCounter = 1024;
 /* No BG consumption in horizontal blanking. */
 fx_vce.in_hblank=1;
 assert(KING_KRAMSlotDrainDeadline(100,100,0) == 116);
 fx_vce.in_hblank=0;
 /* Full BG occupancy waits until active period ends, not a 256-cycle guard. */
 for(int i=4;i<8;i++) king->MPROGData[i]=0;
 assert(KING_KRAMSlotDrainDeadline(100,100,0) == 1140);
 puts("PASS v12: slot grant / A-B isolation / overlap / HBlank / full occupancy");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
