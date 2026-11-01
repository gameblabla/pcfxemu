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
 setenv("PCFXEMU_KRAM_MAPPED_OLD","1",1);   /* the mapped-window 4-grant model is retired (DIAG9 K11/K12 measured ST = OUT) but selectable */
 setenv("PCFXEMU_KRAM_BG_TAIL","0",1);            /* these pin the slot scheduler itself, without the BG-fetch tail fitted to K2 n=8 */
 king = &test;
 king->AR = 0x0e;
 king->priority = 1;
 king->MPROGControl = 1;
 memset(king->MPROGData, 0xff, sizeof(king->MPROGData));
 HPhase = HPHASE_ACTIVE;
 HPhaseCounter = 1024;
 fx_vce.in_hblank = 0;
 fx_vce.raster_counter = 100;		/* BG fetch only steals slots in raster 22..261 (DIAG7 K2) */

 /* ---- memory-mapped (ST.H / ST.W to 0xBC000000) path: the original 4-grant model, kept until DIAG9 K12 measures it ---- */
 king_kram_write_mapped = 1;
 /* Four dot grants at 4 clocks each, no other clients. */
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 116);
 assert(KING_KRAMSlotDrainDeadline(100,100,1,1) == 116);
 /* Bank A: four BG requests, then four slots available for CPU. */
 for(int i=0;i<4;i++) king->MPROGData[i] = 0;
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 132);
 /* Bank B is independent; unoccupied A slots do not burden B. */
 assert(KING_KRAMSlotDrainDeadline(100,100,0,1) == 116);
 /* Putting ALU work between writes hides the latch drain. */
 HPhaseCounter = 988;
 assert(KING_KRAMSlotDrainDeadline(136,136,0,0) == 164);
 HPhaseCounter = 1024;
 /* No BG consumption in horizontal blanking, nor in the vertical blanking lines. */
 fx_vce.in_hblank=1;
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 116);
 fx_vce.in_hblank=0;
 fx_vce.raster_counter = 10;
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 116);
 fx_vce.raster_counter = 100;
 /* Full BG occupancy waits until active period ends, not a 256-cycle guard. */
 for(int i=4;i<8;i++) king->MPROGData[i]=0;
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 1140);

 /* ---- OUT.H path, fitted to the DIAG7/DIAG8 photos of a real console: ONE free dot grant to start, then 8 clocks of latch
  *      recovery (dense OUT.H 567.4 ns = 12.2 clk: 100 -> 112 with no BG; K2 n=4 714.7 ns). ---- */
 king_kram_write_mapped = 0;
 memset(king->MPROGData, 0xff, sizeof(king->MPROGData));
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 112);
 assert(KING_KRAMSlotDrainDeadline(100,100,1,1) == 112);
 for(int i=0;i<4;i++) king->MPROGData[i] = 0;
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 128);	/* four busy slots, then the grant, then the recovery */
 assert(KING_KRAMSlotDrainDeadline(100,100,0,1) == 112);	/* bank B (address bit 17) unaffected */
 HPhaseCounter = 988;
 assert(KING_KRAMSlotDrainDeadline(136,136,0,0) == 160);
 HPhaseCounter = 1024;
 fx_vce.in_hblank=1;
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 112);
 fx_vce.in_hblank=0;
 fx_vce.raster_counter = 10;
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 112);
 fx_vce.raster_counter = 100;
 for(int i=4;i<8;i++) king->MPROGData[i]=0;
 assert(KING_KRAMSlotDrainDeadline(100,100,0,0) == 1136);
 puts("PASS v12: slot grant / A-B isolation / overlap / HBlank / VBlank / full occupancy (mapped 4-grant and OUT.H 1-grant+8)");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
