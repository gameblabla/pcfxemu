/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* VDC emulation */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mednafen/mednafen.h"
#include "mednafen/state_helpers.h"

#include "vdc.h"
#include "../../video/surface.h"

/*static inline void VDC_DEBUG(const char *fmt, ...)
{
}*/
//#define VDC_DEBUG(x, ...)     { }
//#define VDC_DEBUG(x, ...)       printf(x ": HPhase=%d, HPhaseCounter=%d, RCRCount=%d\n", ## __VA_ARGS__, HPhase, HPhaseCounter, RCRCount);
//
/*static inline void VDC_UNDEFINED(const char *fmt, ...)
{
}*/

//#define VDC_UNDEFINED(format, ...)   { }
//#define VDC_UNDEFINED(format, ...)      printf(format " RCRCount=%d" "\n", ## __VA_ARGS__, RCRCount)

/*static inline void VDC_WARNING(const char *fmt, ...)
{
}*/
//#define VDC_WARNING(format, ...)      { }
//#define VDC_WARNING(format, ...)     { printf(format "\n", ## __VA_ARGS__); }

#define ULE_BG		1
#define ULE_SPR		2


static void VDC_CheckAndCommitPending(VDC *vdc);
static void VDC_FetchSpriteData(VDC *vdc);
static void VDC_DrawBG(VDC *vdc, uint16 *target, int enabled);
static void VDC_DrawSprites(VDC *vdc, uint16 *target, int enabled);
static void VDC_CalcWidthStartEnd(VDC *vdc, uint32 *display_width, uint32 *start, uint32 *end);
static const unsigned int bat_width_tab[4] = { 32, 64, 128, 128 };
static const unsigned int bat_width_shift_tab[4] = { 5, 6, 7, 7 };
static const unsigned int bat_height_tab[2] = { 32, 64 };
#define VRAM_Size (vdc->VRAM_Size)
#define VRAM_SizeMask (vdc->VRAM_SizeMask)
#define VRAM_BGTileNoMask (vdc->VRAM_BGTileNoMask)
#define IRQHook (vdc->IRQHook)
#define WSHook (vdc->WSHook)
#define in_exhsync (vdc->in_exhsync)
#define in_exvsync (vdc->in_exvsync)
#define Simulate_select (vdc->Simulate_select)
#define Simulate_MAWR (vdc->Simulate_MAWR)
#define Simulate_MARR (vdc->Simulate_MARR)
#define Simulate_CR (vdc->Simulate_CR)
#define Simulate_LENR (vdc->Simulate_LENR)
#define sat_dma_counter (vdc->sat_dma_counter)
#define select (vdc->select)
#define MAWR (vdc->MAWR)
#define MARR (vdc->MARR)
#define CR (vdc->CR)
#define CR_cache (vdc->CR_cache)
#define RCR (vdc->RCR)
#define BXR (vdc->BXR)
#define BYR (vdc->BYR)
#define MWR (vdc->MWR)
#define HSR (vdc->HSR)
#define HDR (vdc->HDR)
#define VSR (vdc->VSR)
#define VDR (vdc->VDR)
#define VCR (vdc->VCR)
#define DCR (vdc->DCR)
#define SOUR (vdc->SOUR)
#define DESR (vdc->DESR)
#define LENR (vdc->LENR)
#define DVSSR (vdc->DVSSR)
#define VDMA_CycleCounter (vdc->VDMA_CycleCounter)
#define RCRCount (vdc->RCRCount)
#define pending_read (vdc->pending_read)
#define pending_read_addr (vdc->pending_read_addr)
#define read_buffer (vdc->read_buffer)
#define write_latch (vdc->write_latch)
#define pending_write (vdc->pending_write)
#define pending_write_addr (vdc->pending_write_addr)
#define pending_write_latch (vdc->pending_write_latch)
#define status (vdc->status)
#define SAT (vdc->SAT)
#define VRAM (vdc->VRAM)
#define cache (vdc->cache)
#define DMAReadBuffer (vdc->DMAReadBuffer)
#define DMAReadWrite (vdc->DMAReadWrite)
#define DMARunning (vdc->DMARunning)
#define DMAPending (vdc->DMAPending)
#define SATBPending (vdc->SATBPending)
#define burst_mode (vdc->burst_mode)
#define BG_YOffset (vdc->BG_YOffset)
#define BG_XOffset (vdc->BG_XOffset)
#define HSW_cache (vdc->HSW_cache)
#define HDS_cache (vdc->HDS_cache)
#define HDW_cache (vdc->HDW_cache)
#define HDE_cache (vdc->HDE_cache)
#define VDS_cache (vdc->VDS_cache)
#define VSW_cache (vdc->VSW_cache)
#define VDW_cache (vdc->VDW_cache)
#define VCR_cache (vdc->VCR_cache)
#define MWR_cache (vdc->MWR_cache)
#define BG_YMoo (vdc->BG_YMoo)
#define NeedRCRInc (vdc->NeedRCRInc)
#define NeedVBIRQTest (vdc->NeedVBIRQTest)
#define NeedSATDMATest (vdc->NeedSATDMATest)
#define NeedBGYInc (vdc->NeedBGYInc)
#define HPhase (vdc->HPhase)
#define VPhase (vdc->VPhase)
#define HPhaseCounter (vdc->HPhaseCounter)
#define VPhaseCounter (vdc->VPhaseCounter)
#define sprite_cg_fetch_counter (vdc->sprite_cg_fetch_counter)
#define mystery_counter (vdc->mystery_counter)
#define mystery_phase (vdc->mystery_phase)
#define linebuf (vdc->linebuf)
#define pixel_desu (vdc->pixel_desu)
#define pixel_copy_count (vdc->pixel_copy_count)
#define userle (vdc->userle)
#define unlimited_sprites (vdc->unlimited_sprites)
#define active_sprites (vdc->active_sprites)
#define SpriteList (vdc->SpriteList)

#define HPHASE_HDS 0
#define HPHASE_HDS_PART2 1
#define HPHASE_HDS_PART3 2
#define HPHASE_HDW 3
#define HPHASE_HDW_FINAL 4
#define HPHASE_HDE 5
#define HPHASE_HSW 6
#define HPHASE_COUNT 7
#define VPHASE_VDS 0
#define VPHASE_VDW 1
#define VPHASE_VCR 2
#define VPHASE_VSW 3
#define VPHASE_COUNT 4

static inline int32 VDC_CalcNextEvent(VDC *vdc)
{
 int32 next_event = HPhaseCounter;
 if(sat_dma_counter > 0 && sat_dma_counter < next_event) next_event = sat_dma_counter;
 if(sprite_cg_fetch_counter > 0 && sprite_cg_fetch_counter < next_event) next_event = sprite_cg_fetch_counter;
 if(DMARunning)
 {
  int32 next_vram_dma_event = ((LENR + 1) * 4) - (DMAReadWrite * 2) - VDMA_CycleCounter;
  if(next_vram_dma_event > 0 && next_vram_dma_event < next_event) next_event = next_vram_dma_event;
 }
 return next_event;
}


void VDC_FixTileCache(VDC *vdc, uint16 A)
{
 uint32 charname = (A >> 4);
 uint32 y = (A & 0x7);
 uint8 *tc = cache.bg_tile_cache[charname][y];

 uint32 bitplane01 = VRAM[y + charname * 16];
 uint32 bitplane23 = VRAM[y+ 8 + charname * 16];

 for(uint_fast8_t x = 0; x < 8; x++)
 {
  uint32 raw_pixel = ((bitplane01 >> x) & 1);
  raw_pixel |= ((bitplane01 >> (x + 8)) & 1) << 1;
  raw_pixel |= ((bitplane23 >> x) & 1) << 2;
  raw_pixel |= ((bitplane23 >> (x + 8)) & 1) << 3;
  tc[7 - x] = raw_pixel;
 }
}

// Some virtual vdc macros to make code simpler to read
#define M_vdc_HSW	(HSR & 0x1F)	// Horizontal Synchro Width
#define M_vdc_HDS	((HSR >> 8) & 0x7F) // Horizontal Display Start
#define M_vdc_HDW	(HDR & 0x7F)	// Horizontal Display Width
#define M_vdc_HDE	((HDR >> 8) & 0x7F) // Horizontal Display End

#define M_vdc_VSW	(VSR & 0x1F)	// Vertical synchro width
#define M_vdc_VDS	((VSR >> 8) & 0xFF) // Vertical Display Start
#define M_vdc_VDW	(VDR & 0x1FF)	// Vertical Display Width(Height? :b)
#define M_vdc_VCR	(VCR & 0xFF)

#define M_vdc_EX	((CR >> 4) & 0x3)
#define M_vdc_TE	((CR >> 8) & 0x3)

#define VDCS_CR		0x01 // Sprite #0 collision interrupt occurred
#define VDCS_OR		0x02 // sprite overflow "" ""
#define VDCS_RR		0x04 // RCR             ""  ""
#define VDCS_DS		0x08 // VRAM to SAT DMA completion interrupt occurred
#define VDCS_DV		0x10 // VRAM to VRAM DMA completion interrupt occurred
#define VDCS_VD		0x20 // Vertical blank interrupt occurred
#define VDCS_BSY	0x40 // VDC is waiting for a CPU access slot during the active display area??

void VDC_SetLayerEnableMask(VDC *vdc, uint64 mask)
{
 userle = mask;
}

void VDC_RunSATDMA(VDC *vdc, int32 cycles, bool force_completion)
{
 //assert(sat_dma_counter > 0);

 if(force_completion)
  cycles = sat_dma_counter;

 sat_dma_counter -= cycles;
 if(sat_dma_counter <= 0)
 {
  if(DCR & 0x01)
  {
   //VDC_DEBUG("Sprite DMA IRQ");
   status |= VDCS_DS;
   IRQHook(TRUE);
  }
  VDC_CheckAndCommitPending(vdc);
  burst_mode = true;
 }
}

void VDC_RunDMA(VDC *vdc, int32 cycles, bool force_completion)
{
 int num_transfers = 0;

 if(force_completion)
 {
  VDMA_CycleCounter = 0;

  num_transfers = 65536 * 2;
 }
 else
 {
  VDMA_CycleCounter += cycles;
  num_transfers = VDMA_CycleCounter >> 1;
  VDMA_CycleCounter -= num_transfers << 1;
 }

 while(num_transfers--)
 {
  if(!DMAReadWrite)
  {
   /*if(SOUR >= VRAM_Size)
    VDC_UNDEFINED("Unmapped VRAM DMA read");*/

   DMAReadBuffer = VRAM[SOUR];
   //printf("DMA Read: %04x, %04x\n", SOUR, DMAReadBuffer);
  }
  else
  {
   if(DESR < VRAM_Size)
   {
    VRAM[DESR] = DMAReadBuffer;
    VDC_FixTileCache(vdc, DESR);
   }

   SOUR += (((DCR & 0x4) >> 1) ^ 2) - 1;
   DESR += (((DCR & 0x8) >> 2) ^ 2) - 1;
   LENR--;

   if(LENR == 0xFFFF)  // DMA is done.
   {
    DMARunning = 0;	// Clear this BEFORE VDC_CheckAndCommitPending(vdc)

    VDC_CheckAndCommitPending(vdc);

    if(DCR & 0x02)
    {
     status |= VDCS_DV;
     IRQHook(TRUE);
     //VDC_DEBUG("DMA IRQ");
    }
    break;
   }
  }
  DMAReadWrite ^= 1;
 }
}

/*
<RyphZomb> ChrlyMac: Was it you who determined exactly how many VDC clocks the SAT DMA took?
<RyphZomb> I know someone did, but I can't remember the results...
<ChrlyMac> 1024
<ChrlyMac> It happens at the VDW->VCR transition
*/

void VDC_IncRCR(VDC *vdc)
{
 if(NeedBGYInc)
 {
  NeedBGYInc = false;
  if(0 == RCRCount)
   BG_YMoo = BYR;
  else
   BG_YMoo++;
 }

 NeedBGYInc = true;
 RCRCount++;

 VPhaseCounter--;

 if(VPhaseCounter <= 0)
 {
  VPhase = (VPhase + 1) % VPHASE_COUNT;
  switch(VPhase)
  {
   case VPHASE_VDS: VPhaseCounter = VDS_cache + 2;
		    break;

   case VPHASE_VDW: VPhaseCounter = VDW_cache + 1;
		    //BG_YMoo = BYR - 1;
		    RCRCount = 0;
		    burst_mode = !(CR & 0xC0);
		    NeedVBIRQTest = true;
		    NeedSATDMATest = true;

		    if(!burst_mode)
		    {
		     if(sat_dma_counter > 0)
		     {
		      //printf("SAT DMA cancelled???\n");
		      sat_dma_counter = 0;
		      VDC_CheckAndCommitPending(vdc);
		     }
		     if(DMARunning)
		     {
		      //printf("DMA Running Cancelled\n");
		      DMARunning = false;
		      VDC_CheckAndCommitPending(vdc);
		     }
		    }
		    break;

   case VPHASE_VCR: VPhaseCounter = VCR_cache;
		    break;

   case VPHASE_VSW: VPhaseCounter = VSW_cache + 1;
		    MWR_cache = MWR;
		    VDS_cache = M_vdc_VDS;
		    VSW_cache = M_vdc_VSW;
		    VDW_cache = M_vdc_VDW;
		    VCR_cache = M_vdc_VCR;
		    //VDC_WARNING("VSW Started");
		    break;
  }
 }

 if(VPhase == VPHASE_VDW && !burst_mode)
 {
  VDC_FetchSpriteData(vdc);
 }

 if((int)RCRCount == ((int)RCR - 0x40) && (CR & 0x04))
 {
  //VDC_DEBUG("RCR IRQ");
  status |= VDCS_RR;
  IRQHook(TRUE);
 }
}

void VDC_DoVBIRQTest(VDC *vdc)
{
 if(CR & 0x08)
 {
  //VDC_DEBUG("VBlank IRQ");
  status |= VDCS_VD;
  IRQHook(TRUE);
 }
}

static const int Cycles_Between_RCRIRQ_And_HDWEnd = 4;

static int VDC_TimeFromHDSStartToBYRLatch(VDC *vdc)
{
 int ret = 1;

 if(HDS_cache > 2)
  ret += ((HDS_cache + 1) * 8) - 24 + 2;


 //printf("%d, %d\n", HDS_cache, ret);

 return(ret);
}

static int VDC_TimeFromBYRLatchToBXRLatch(VDC *vdc)
{
 int ret = 2;

 if(HDS_cache > 2)
  ret = 1;

 return(ret);
}

void VDC_HDS_Start(VDC *vdc)
{
 if(NeedRCRInc)
 {
  VDC_IncRCR(vdc);
  NeedRCRInc = false;
 }

 if(sprite_cg_fetch_counter > 0)
 {
  //VDC_WARNING("Sprite truncation on %d.  Wanted sprites: %d, cycles needed but not left: %d\n", RCRCount, active_sprites, sprite_cg_fetch_counter);
  sprite_cg_fetch_counter = 0;
  VDC_CheckAndCommitPending(vdc);
 }

 HSW_cache = M_vdc_HSW;
 HDS_cache = M_vdc_HDS;
 HDW_cache = M_vdc_HDW;
 HDE_cache = M_vdc_HDE;

 //VDC_DEBUG("HDS Start!  HSW: %d, HDW: %d, HDW: %d, HDE: %d\n", HSW_cache, HDS_cache, HDW_cache, HDE_cache);

 CR_cache = CR;

 HPhase = HPHASE_HDS;
 HPhaseCounter = VDC_TimeFromHDSStartToBYRLatch(vdc);
}

int32 VDC_HSync(VDC *vdc, bool hb)
{
 if(M_vdc_EX)
 {
  in_exhsync = 0;
  return(VDC_CalcNextEvent(vdc));
 }
 in_exhsync = hb;

 if(hb) // Going into hsync
 {
  mystery_counter = 48;
  mystery_phase = false;
 }
 else // Leaving hsync
 {
  HPhase = HPHASE_HSW;
  HPhaseCounter = 8;

  //VDC_HDS_Start(vdc);
  //HPhaseCounter += 8;

  pixel_copy_count = 0;
 }


 return(VDC_CalcNextEvent(vdc));
}

int32 VDC_VSync(VDC *vdc, bool vb)
{
 if(M_vdc_EX >= 0x2)
 {
  in_exvsync = 0;
  return(VDC_CalcNextEvent(vdc));
 }
 in_exvsync = vb;

 //printf("VBlank: %d\n", vb);
 if(vb) // Going into vsync
 {
  NeedRCRInc = false;
  NeedBGYInc = false;
/*  if(NeedRCRInc)
  {
   VDC_IncRCR(vdc);
   NeedRCRInc = false;
  }
*/
  MWR_cache = MWR;

  VDS_cache = M_vdc_VDS;
  VSW_cache = M_vdc_VSW;
  VDW_cache = M_vdc_VDW;
  VCR_cache = M_vdc_VCR;

  VPhase = VPHASE_VSW;
  VPhaseCounter = VSW_cache + 1;
 }
 /*else	// Leaving vsync
 {

 }*/
 return(VDC_CalcNextEvent(vdc));
}

//int32 VDC::Run(int32 clocks, bool hs, bool vs, uint16 *pixels, bool skip)
int32 VDC_Run(VDC *vdc, int32 clocks, uint16 *pixels, bool skip)
{
 //uint16 *spixels = pixels;

 //puts("Run begin");
 //fflush(stdout);

 while(clocks > 0)
 {
  int32 chunk_clocks = clocks;

  if(chunk_clocks > HPhaseCounter)
  {
   chunk_clocks = HPhaseCounter;
  }

  if(sat_dma_counter > 0 && chunk_clocks > sat_dma_counter)
   chunk_clocks = sat_dma_counter;

  if(sprite_cg_fetch_counter > 0 && chunk_clocks > sprite_cg_fetch_counter)
   chunk_clocks = sprite_cg_fetch_counter;

  if(mystery_counter > 0 && chunk_clocks > mystery_counter)
   chunk_clocks = mystery_counter;

  if(mystery_counter > 0)
  {
   mystery_counter -= chunk_clocks;
   if(mystery_counter <= 0)
   {
    mystery_phase = !mystery_phase;
    if(mystery_phase)
     mystery_counter = 16;
    else
     VDC_CheckAndCommitPending(vdc);
   }
  }

  if(sprite_cg_fetch_counter > 0)
  {
   sprite_cg_fetch_counter -= chunk_clocks;
   if(sprite_cg_fetch_counter <= 0)
    VDC_CheckAndCommitPending(vdc);
  }

  if(VPhase != VPHASE_VDW)
  {
   if(NeedSATDMATest)
   {
    NeedSATDMATest = false;
    if(SATBPending || (DCR & 0x10))
    {
        SATBPending = 0;

        sat_dma_counter = 1024;

        /*if(DVSSR > (VRAM_Size - 0x100))
         VDC_UNDEFINED("Unmapped VRAM DVSSR DMA read");*/

        if(DVSSR < VRAM_Size)
        {
         uint32 len = 256;
         if(DVSSR > (VRAM_Size - 0x100))
          len = VRAM_Size - DVSSR;
         memcpy(SAT, &VRAM[DVSSR], len * sizeof(uint16));
        }
    }
   }
  }




  if(DMAPending && burst_mode)
  {
   //VDC_DEBUG("DMA Started");
   DMAPending = false;
   DMARunning = true;
   VDMA_CycleCounter = 0;
   DMAReadWrite = 0;
  }

  if(sat_dma_counter > 0)
   VDC_RunSATDMA(vdc, chunk_clocks, false);
  else if(DMARunning)
   VDC_RunDMA(vdc, chunk_clocks, false);

  if(pixel_copy_count > 0)
  {
   if(!skip)
   {
    for(int i = 0; i < chunk_clocks; i++)
     pixels[i] = linebuf[pixel_desu + i];
    //memcpy(pixels, linebuf + pixel_desu, chunk_clocks * sizeof(uint16));

    if(M_vdc_TE == 0x1)
     for(int i = 0; i < chunk_clocks; i++)
      pixels[i] |= VDC_DISP_OUT_MASK;
   }

   pixel_desu += chunk_clocks;
   pixel_copy_count -= chunk_clocks;
  }
  else
  {
   uint16 pix = 0x100;

   if(M_vdc_TE == 0x1)
   {
    if(HPhase != HPHASE_HDS && HPhase != HPHASE_HDS_PART2 && HPhase != HPHASE_HDS_PART3)
     pix |= VDC_DISP_OUT_MASK;
   }

   if(HPhase == HPHASE_HSW)
   {
    if(M_vdc_EX >= 0x1)
     pix |= VDC_HSYNC_OUT_MASK;
 
    if(M_vdc_TE >= 0x2)
     pix |= VDC_DISP_OUT_MASK;
   }
   if(VPhase == VPHASE_VSW && M_vdc_EX >= 0x2)
    pix |= VDC_VSYNC_OUT_MASK;

   if(!(userle & 1))
    pix |= VDC_BGDISABLE_OUT_MASK;

   if(!skip)
   {
    for(int i = 0; i < chunk_clocks; i++)
     pixels[i] = pix;
   }
  }

  HPhaseCounter -= chunk_clocks;

  //assert(HPhaseCounter >= 0);

  while(HPhaseCounter <= 0)
  {
   HPhase = (HPhase + 1) % HPHASE_COUNT;
  
   switch(HPhase)
   { 
    case HPHASE_HDS: VDC_HDS_Start(vdc);
		     break;


    case HPHASE_HDS_PART2:
                     HPhaseCounter = VDC_TimeFromBYRLatchToBXRLatch(vdc);	

		     if(NeedBGYInc && !in_exhsync)
		     {
		      NeedBGYInc = false;

		      if(0 == RCRCount)
		       BG_YMoo = BYR;
		      else
		       BG_YMoo++;
		     }
                     BG_YOffset = BG_YMoo;
		     break;

    case HPHASE_HDS_PART3:
		     HPhaseCounter = (HDS_cache + 1) * 8 - VDC_TimeFromHDSStartToBYRLatch(vdc) - VDC_TimeFromBYRLatchToBXRLatch(vdc);

		     //assert(HPhaseCounter > 0);

		     BG_XOffset = BXR;
		     break;

    case HPHASE_HDW: 
		     NeedRCRInc = true;
		     if(VPhase != VPHASE_VDW && NeedVBIRQTest)
		     {
		      VDC_DoVBIRQTest(vdc);
		      NeedVBIRQTest = false;
		     }
                     VDC_CheckAndCommitPending(vdc);

		     HPhaseCounter = (HDW_cache + 1) * 8 - Cycles_Between_RCRIRQ_And_HDWEnd;
		     if(VPhase == VPHASE_VDW)
		     {
		      if(!burst_mode)
		      {
		       pixel_desu = 0;
		       pixel_copy_count = (HDW_cache + 1) * 8;

		       // BG off, sprite on: fill = 0x100.  bg off, sprite off: fill = 0x000
		       if(!(CR_cache & 0x80))
		       {
		        uint16 fill_val;

		        if(!(CR_cache & 0xC0))	// Sprites and BG off
			 fill_val = 0x000;			
		        else	// Only BG off
			 fill_val = 0x100 | ((userle & ULE_BG) ? 0 : VDC_BGDISABLE_OUT_MASK);

			if(!(userle & ULE_BG))
			 fill_val |= VDC_BGDISABLE_OUT_MASK;

			for(int i = 0; i < pixel_copy_count; i++)
			 linebuf[i] = fill_val;
		       }

		       if(!skip)
 		        if(CR_cache & 0x80)
			{
		         VDC_DrawBG(vdc, linebuf, userle & ULE_BG);
			}
			//printf("%d %02x %02x\n", RCRCount, CR, CR_cache);
		       if(CR_cache & 0x40)
		        VDC_DrawSprites(vdc, linebuf, (userle & ULE_SPR) && !skip);
		      }
		     }
		     break;

    case HPHASE_HDW_FINAL:
		     if(NeedRCRInc)
		     {
		      VDC_IncRCR(vdc);
		      NeedRCRInc = false;
		     }
		     HPhaseCounter = Cycles_Between_RCRIRQ_And_HDWEnd;
		     break;

    case HPHASE_HDE: //if(!burst_mode) //if(VPhase == VPHASE_VDW)	//if(!burst_mode)
		     // lastats = 16;	// + 16;
		     //else
		     // lastats = 16;
		     HPhaseCounter = (HDE_cache + 1) * 8;
		     break;

    case HPHASE_HSW: HPhaseCounter = (HSW_cache + 1) * 8; break;
   }
  }
  pixels += chunk_clocks;
  clocks -= chunk_clocks;
 }

 //puts("Run end");
 //fflush(stdout);

 return(VDC_CalcNextEvent(vdc));
}


static void VDC_CalcWidthStartEnd(VDC *vdc, uint32 *display_width, uint32 *start, uint32 *end)
{
 *display_width = (M_vdc_HDW + 1) * 8;
 if(*display_width > 512)
  *display_width = 512;

 *start = 0;
 *end = *start + *display_width;
}

static void VDC_DrawBG(VDC *vdc, uint16 *target, int enabled)
{
 uint32 width;
 uint32 start;
 uint32 end;
 int bat_width = bat_width_tab[(MWR_cache >> 4) & 3];
 int bat_width_mask = bat_width - 1;
 int bat_width_shift = bat_width_shift_tab[(MWR_cache >> 4) & 3];
 int bat_height_mask = bat_height_tab[(MWR_cache >> 6) & 1] - 1;

 VDC_CalcWidthStartEnd(vdc, &width, &start, &end);

 if(!enabled)
 {
  for(uint32 x = start; x < end; x++)
   target[x] = 0x000 | VDC_BGDISABLE_OUT_MASK;
  return;
 }

 {
  int bat_y = ((BG_YOffset >> 3) & bat_height_mask) << bat_width_shift;
  uint32 first_end = start + 8 - (BG_XOffset & 7);
  uint32 dohmask = 0xFFFFFFFF;

  if((MWR_cache & 0x3) == 0x3)
  {
   if(MWR_cache & 0x80)
    dohmask = 0xCCCCCCCC;
   else
    dohmask = 0x33333333;
  }

  // Draw the first pixels of the first tile, depending on the lower 3 bits of the xscroll/xoffset register, to
  // we can render the rest of the line in 8x1 chunks, which is faster.
  for(uint32 x = start; x < first_end; x++)
  {
   int bat_x = (BG_XOffset >> 3) & bat_width_mask;
   uint16 bat = VRAM[bat_x | bat_y];
   const uint8 pal_or = ((bat >> 8) & 0xF0);
   int palette_index = ((bat >> 12) & 0x0F) << 4;
   uint32 raw_pixel;

   raw_pixel = cache.bg_tile_cache[bat & 0xFFF][BG_YOffset & 7][BG_XOffset & 0x7] & dohmask;
   target[x] = palette_index | raw_pixel | pal_or;

   /*if((bat & 0xFFF) > VRAM_BGTileNoMask)
    VDC_UNDEFINED("Unmapped BG tile read");*/

   BG_XOffset++;
  }

  int bat_boom = (BG_XOffset >> 3) & bat_width_mask;
  int line_sub = BG_YOffset & 7;

  if((MWR_cache & 0x3) == 0x3)
  {
   for(uint32 x = first_end; x < end; x+=8)
   {
    const uint16 bat = VRAM[bat_boom | bat_y];
    const uint8 pal_or = ((bat >> 8) & 0xF0);
    uint8 *pix_lut = cache.bg_tile_cache[bat & 0xFFF][line_sub];

    /*if((bat & 0xFFF) > VRAM_BGTileNoMask)
     VDC_UNDEFINED("Unmapped BG tile read");*/


    (target + 0)[x] = (pix_lut[0] & dohmask) | pal_or;
    (target + 1)[x] = (pix_lut[1] & dohmask) | pal_or;
    (target + 2)[x] = (pix_lut[2] & dohmask) | pal_or;
    (target + 3)[x] = (pix_lut[3] & dohmask) | pal_or;
    (target + 4)[x] = (pix_lut[4] & dohmask) | pal_or;
    (target + 5)[x] = (pix_lut[5] & dohmask) | pal_or;
    (target + 6)[x] = (pix_lut[6] & dohmask) | pal_or;
    (target + 7)[x] = (pix_lut[7] & dohmask) | pal_or;

    bat_boom = (bat_boom + 1) & bat_width_mask;
    BG_XOffset++;

   }
  }
  else
  for(uint32 x = first_end; x < end; x+=8) // This will draw past the right side of the buffer, but since our pitch is 1024, and max width is ~512, we're safe.  Also,
					// any overflow that is on the visible screen are will be hidden by the overscan color code below this code.
  {
   const uint16 bat = VRAM[bat_boom | bat_y];
   const uint8 pal_or = ((bat >> 8) & 0xF0);
   uint8 *pix_lut = cache.bg_tile_cache[bat & 0xFFF][line_sub];

   /*if((bat & 0xFFF) > VRAM_BGTileNoMask)
    VDC_UNDEFINED("Unmapped BG tile read");*/

#ifdef MSB_FIRST
   (target + 0)[x] = pix_lut[0] | pal_or;
   (target + 1)[x] = pix_lut[1] | pal_or;
   (target + 2)[x] = pix_lut[2] | pal_or;
   (target + 3)[x] = pix_lut[3] | pal_or;
   (target + 4)[x] = pix_lut[4] | pal_or;
   (target + 5)[x] = pix_lut[5] | pal_or;
   (target + 6)[x] = pix_lut[6] | pal_or;
   (target + 7)[x] = pix_lut[7] | pal_or;
#else
#if SIZEOF_LONG == 8
   uint64 doh = *(uint64 *)pix_lut;

   (target + 0)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 1)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 2)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 3)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 4)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 5)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 6)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 7)[x] = (doh) | pal_or;
#else
   uint32 doh = *(uint32 *)pix_lut;
   (target + 0)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 1)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 2)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 3)[x] = doh | pal_or;
   doh = *(uint32 *)(pix_lut + 4);
   (target + 4)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 5)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 6)[x] = (doh & 0xFF) | pal_or;
   doh >>= 8;
   (target + 7)[x] = doh | pal_or;
#endif
#endif

   bat_boom = (bat_boom + 1) & bat_width_mask;
   BG_XOffset++;
  }
 }
}

#define SPRF_PRIORITY	0x00080
#define SPRF_HFLIP	0x00800
#define SPRF_VFLIP	0x08000
#define SPRF_SPRITE0	0x10000

static const unsigned int sprite_height_tab[4] = { 16, 32, 64, 64 };
static const unsigned int sprite_height_no_mask[4] = { ~0U, ~2U, ~6U, ~6U };
static const unsigned int sprite_width_tab[2] = { 16, 32 };

static void VDC_FetchSpriteData(VDC *vdc)
{
 active_sprites = 0;

 // First, grab the up to 16 sprites.
 for(int i = 0; i < 64; i++)
 {
  int16 y = (SAT[i * 4 + 0] & 0x3FF) - 0x40;
  uint16 x = (SAT[i * 4 + 1] & 0x3FF);
  uint16 no = (SAT[i * 4 + 2] >> 1) & 0x3FF;	// Todo, cg mode bit
  uint16 flags = (SAT[i * 4 + 3]);

  uint32 palette_index = (flags & 0xF) << 4;
  uint32 height = sprite_height_tab[(flags >> 12) & 3];
  uint32 width = sprite_width_tab[(flags >> 8) & 1];

  if((int32)RCRCount >= y && (int32)RCRCount < (int32)(y + height))
  {
   bool second_half = 0;
   uint32 y_offset = RCRCount - y;
   if(y_offset > height) continue;


   breepbreep:

   if(active_sprites == 16)
   {
    if(CR & 0x2)
    {
     status |= VDCS_OR;
     IRQHook(TRUE);
    // VDC_DEBUG("Overflow IRQ");
    }
    if(!unlimited_sprites)
     break;
   }


   {
    if(flags & SPRF_VFLIP)
     y_offset = height - 1 - y_offset;

    no &= sprite_height_no_mask[(flags >> 12) & 3];
    no |= (y_offset & 0x30) >> 3;
    if(width == 32) no &= ~1;
    if(second_half)
     no |= 1;

    SpriteList[active_sprites].flags = flags;

    if(flags & SPRF_HFLIP && width == 32)
     no ^= 1;
    //printf("Found: %d %d\n", RCRCount, x);
    SpriteList[active_sprites].x = x;
    SpriteList[active_sprites].palette_index = palette_index;

   /*if((no * 64) >= VRAM_Size)
     VDC_UNDEFINED("Unmapped VRAM sprite tile read");*/

    if((MWR_cache & 0xC) == 4)
    {
     if(SAT[i * 4 + 2] & 1)
     {
      SpriteList[active_sprites].pattern_data[0] = VRAM[no * 64 + (y_offset & 15) + 32];
      SpriteList[active_sprites].pattern_data[1] = VRAM[no * 64 + (y_offset & 15) + 48];
      SpriteList[active_sprites].pattern_data[2] = 0; 
      SpriteList[active_sprites].pattern_data[3] = 0;
     }
     else
     {
      SpriteList[active_sprites].pattern_data[0] = VRAM[no * 64 + (y_offset & 15) ];
      SpriteList[active_sprites].pattern_data[1] = VRAM[no * 64 + (y_offset & 15) + 16];
      SpriteList[active_sprites].pattern_data[2] = 0;
      SpriteList[active_sprites].pattern_data[3] = 0;
     }
    }
    else
    {
     SpriteList[active_sprites].pattern_data[0] = VRAM[no * 64 + (y_offset & 15) ];
     SpriteList[active_sprites].pattern_data[1] = VRAM[no * 64 + (y_offset & 15) + 16];
     SpriteList[active_sprites].pattern_data[2] = VRAM[no * 64 + (y_offset & 15) + 32];
     SpriteList[active_sprites].pattern_data[3] = VRAM[no * 64 + (y_offset & 15) + 48];
    }

    SpriteList[active_sprites].flags |= i ? 0 : SPRF_SPRITE0;

    active_sprites++;

    if(width == 32 && !second_half)
    {
     second_half = 1;
     x += 16;
     y_offset = RCRCount - y;	// Fix the y offset so that sprites that are hflipped + vflipped display properly
     goto breepbreep;
    }
   }
  }
 }

 sprite_cg_fetch_counter = ((active_sprites < 16) ? active_sprites : 16) * 4;
}

static void VDC_DrawSprites(VDC *vdc, uint16 *target, int enabled)
{
 MDFN_ALIGN(16) uint16 sprite_line_buf[1024];

 uint32 display_width, start, end;

 VDC_CalcWidthStartEnd(vdc, &display_width, &start, &end);

 for(unsigned int i = start; i < end; i++)
  sprite_line_buf[i] = 0;

 for(int i = (active_sprites - 1) ; i >= 0; i--)
 {
  int32 pos = SpriteList[i].x - 0x20 + start;
  uint32 prio_or = 0;

  if(SpriteList[i].flags & SPRF_PRIORITY) 
   prio_or = 0x200;

  if((SpriteList[i].flags & SPRF_SPRITE0) && (CR & 0x01))
  {
   for(uint32 x = 0; x < 16; x++)
   {
    uint32 raw_pixel;
    uint32 pi = SpriteList[i].palette_index;
    uint32 rev_x = 15 - x;

    if(SpriteList[i].flags & SPRF_HFLIP)
     rev_x = x;

    raw_pixel = (SpriteList[i].pattern_data[0] >> rev_x)  & 1;
    raw_pixel |= ((SpriteList[i].pattern_data[1] >> rev_x) & 1) << 1;
    raw_pixel |= ((SpriteList[i].pattern_data[2] >> rev_x) & 1) << 2;
    raw_pixel |= ((SpriteList[i].pattern_data[3] >> rev_x) & 1) << 3;

    if(raw_pixel)
    {
     pi |= 0x100;
     uint32 tx = pos + x;

     if(tx >= end) // Covers negative and overflowing the right side.
      continue;

     if(sprite_line_buf[tx] & 0xF)
     {
      status |= VDCS_CR;
      //VDC_DEBUG("Sprite hit IRQ");
      IRQHook(TRUE);
     }
     sprite_line_buf[tx] = pi | raw_pixel | prio_or;
    }
   }
  } // End sprite hit loop
  else
  {
   for(uint32 x = 0; x < 16; x++)
   {
    uint32 raw_pixel;
    uint32 pi = SpriteList[i].palette_index;
    uint32 rev_x = 15 - x;

    if(SpriteList[i].flags & SPRF_HFLIP)
     rev_x = x;

    raw_pixel = (SpriteList[i].pattern_data[0] >> rev_x)  & 1;
    raw_pixel |= ((SpriteList[i].pattern_data[1] >> rev_x) & 1) << 1;
    raw_pixel |= ((SpriteList[i].pattern_data[2] >> rev_x) & 1) << 2;
    raw_pixel |= ((SpriteList[i].pattern_data[3] >> rev_x) & 1) << 3;

    if(raw_pixel)
    {
     pi |= 0x100;
     uint32 tx = pos + x;

     if(tx >= end) // Covers negative and overflowing the right side.
      continue;
     sprite_line_buf[tx] = pi | raw_pixel | prio_or;
    }
   }
  } // End non-sprite-hit loop
 }

 if(enabled)
 {
  for(unsigned int x = start; x < end; x++)
  {
   if(sprite_line_buf[x] & 0x0F)
   {
    if(!(target[x] & 0x0F) || (sprite_line_buf[x] & 0x200))
     target[x] = sprite_line_buf[x] & 0x1FF;
   }
  }
 }
 active_sprites = 0;
}

/*
 Caution: If we ever add something to Write() or Read() that will affect the timing of the next event, make sure
 to set the passed-by-reference next_event BEFORE calling this function, or otherwise re-engineer this convoluted setup.
*/
static void VDC_DoWaitStates(VDC *vdc)
{
 //bool did_wait = (pending_read || pending_write);

 while((pending_read || pending_write))
 {
  //int32 to_wait = VDC_CalcNextEvent(vdc);
  //if(!WSHook || !WSHook(to_wait))
  if(!WSHook || !WSHook(-1))	// Event-counter-based wait-stating
  {
   if(DMARunning)
   {
    //VDC_WARNING("VRAM DMA completion forced.");
    VDC_RunDMA(vdc, 0, TRUE);
   }

   if(sat_dma_counter > 0)
   {
    //VDC_WARNING("SAT DMA completion forced.");
    VDC_RunSATDMA(vdc, 0, TRUE);
   }

   if(mystery_phase)
   {
    bool backup_mystery_phase = mystery_phase;
    mystery_phase = false;
    VDC_CheckAndCommitPending(vdc);
    mystery_phase = backup_mystery_phase;
   }

   break;
  }
 }

 //if(did_wait)
 // printf("End of wait stating: %d %d\n", VDMA_CycleCounter, sat_dma_counter);

 //assert(!pending_read);
 //assert(!pending_write);
}

uint8 VDC_Read(VDC *vdc, uint32 A, bool peek)
{
 uint8 ret = 0;
 int msb = A & 1;

 A &= 0x3;

 switch(A)
 {
  case 0x0: ret = status | ((pending_read || pending_write) ? 0x40 : 0x00);

            if(!peek)
            {
             status &= ~0x3F;
             IRQHook(FALSE);
            }
            break;

  case 0x2:
  case 0x3:
	   if(!peek)
	   {
	    // Should we only wait on MSB reads...
	    VDC_DoWaitStates(vdc); 
	   }

           ret = VDC_REGGETP(read_buffer, msb);

           if(select == 0x2) // VRR - VRAM Read Register
           {
            if(msb)
            {
             if(!peek)
             {
	      pending_read = TRUE;
	      pending_read_addr = MARR;
	      MARR += vram_inc_tab[(CR >> 11) & 0x3];

	      VDC_CheckAndCommitPending(vdc);
             }
            }
       	   }
           break;
 }

 return(ret);
}

uint16 VDC_Read16(VDC *vdc, bool A, bool peek)
{
 uint16 ret = 0;

 if(!A)
 {
  ret = status | ((pending_read || pending_write) ? 0x40 : 0x00);

  if(!peek)
  {
   status &= ~0x3F;
   IRQHook(FALSE);
  }
 }
 else
 {
  if(!peek)
   VDC_DoWaitStates(vdc); 

  ret = read_buffer;

  if(select == 0x2) // VRR - VRAM Read Register
  {
   if(!peek)
   {
    pending_read = TRUE;
    pending_read_addr = MARR;
    MARR += vram_inc_tab[(CR >> 11) & 0x3];

    VDC_CheckAndCommitPending(vdc);
   }
  }
 }

 return(ret);
}


static void VDC_CheckAndCommitPending(VDC *vdc)
{
 if(sat_dma_counter <= 0 && !DMARunning /* && sprite_cg_fetch_counter <= 0*/ && !mystery_phase)
 {
  if(pending_write)
  {
   if(pending_write_addr < VRAM_Size)
   {
    VRAM[pending_write_addr] = pending_write_latch;
    VDC_FixTileCache(vdc, pending_write_addr);
   }
   //else
   // VDC_UNDEFINED("Unmapped VRAM write");

   pending_write = FALSE;
  }

  if(pending_read)
  {
   /*if(pending_read_addr >= VRAM_Size)
    VDC_UNDEFINED("Unmapped VRAM VRR read");*/

   read_buffer = VRAM[pending_read_addr];
   pending_read = FALSE;
  }
 }
}


void VDC_Write(VDC *vdc, uint32 A, uint8 V)
{
 int msb = A & 1;

 A &= 0x3;

 //if((A == 0x2 || A == 0x3) && (select >= 0xF && select <= 0x12))
 //if((A == 2 || A == 3) && select != 2)
 // printf("VDC Write(RCRCount=%d): A=%02x, Select=%02x, V=%02x\n", RCRCount, A, select, V);

 switch(A)
 {
  case 0x0: select = V & 0x1F;
	    break;

  case 0x2:
  case 0x3:
	   //if((select & 0x1F) >= 0x9 && (select & 0x1F) <= 0x1F)
	   //	VDC_DEBUG("%02x %d, %02x", select & 0x1F, msb, V);

           switch(select & 0x1F)
           {
            case 0x00: VDC_REGSETP(MAWR, V, msb);
		       break;

            case 0x01: VDC_REGSETP(MARR, V, msb);
       	               if(msb)
                       {
			VDC_DoWaitStates(vdc);

			pending_read = TRUE;
			pending_read_addr = MARR;
	                MARR += vram_inc_tab[(CR >> 11) & 0x3];

			VDC_CheckAndCommitPending(vdc);
                       }
                       break;

            case 0x02: if(!msb) 
		       {
			write_latch = V;
		       }
                       else
                       {
			// We must call CommitPendingWrite at the end of SAT/VRAM DMA for this to work!
			VDC_DoWaitStates(vdc);

			pending_write = TRUE;
			pending_write_addr = MAWR;
			pending_write_latch = write_latch | (V << 8);
	                MAWR += vram_inc_tab[(CR >> 11) & 0x3];

			VDC_CheckAndCommitPending(vdc);
                       }
                       break;

            case 0x05: VDC_REGSETP(CR, V, msb);
			//printf("CR: %04x, %d\n", CR, msb);
                       break;

            case 0x06: VDC_REGSETP(RCR, V, msb);
		       RCR &= 0x3FF;
		       break;

            case 0x07: VDC_REGSETP(BXR, V, msb);
                       BXR &= 0x3FF;
                       //VDC_DEBUG("BXR Set");
                       break;

            case 0x08: VDC_REGSETP(BYR, V, msb);
		       BYR &= 0x1FF;
                       BG_YMoo = BYR; // Set it on LSB and MSB writes(only changing on MSB breaks Youkai Douchuuki)
                       //VDC_DEBUG("BYR Set");
                       break;

            case 0x09: VDC_REGSETP(MWR, V, msb); break;
       	    case 0x0a: VDC_REGSETP(HSR, V, msb); break;
            case 0x0b: VDC_REGSETP(HDR, V, msb); break;
       	    case 0x0c: VDC_REGSETP(VSR, V, msb); break;
            case 0x0d: VDC_REGSETP(VDR, V, msb); break;
       	    case 0x0e: VDC_REGSETP(VCR, V, msb); break;
            case 0x0f: VDC_REGSETP(DCR, V, msb);
                       /*if(DMARunning)
                       {
                        VDC_UNDEFINED("Set DCR during DMA: %04x\n", DCR);
                       }

                       if(DMAPending)
                       {
                        VDC_UNDEFINED("Set DCR while DMAPending: %04x\n", DCR);
                       }*/

		       break;

            case 0x10: VDC_REGSETP(SOUR, V, msb); 
		       /*if(DMARunning)
		       {
		        VDC_UNDEFINED("Set SOUR during DMA: %04x\n", SOUR);
		       }

                       if(DMAPending)
                       {
                        VDC_UNDEFINED("Set SOUR while DMAPending: %04x\n", SOUR);
                       }*/
		       break;

            case 0x11: VDC_REGSETP(DESR, V, msb);
                       /*if(DMARunning)
                       {
                        VDC_UNDEFINED("Set DESR during DMA: %04x\n", DESR);
                       }
                       if(DMAPending)
                       {
                        VDC_UNDEFINED("Set DESR while DMAPending: %04x\n", DESR);
                       }*/
		       break;

            case 0x12: VDC_REGSETP(LENR, V, msb);
                       /*if(DMARunning)
                       {
                        VDC_UNDEFINED("Set LENR during DMA: %04x\n", LENR);
                       }

                       if(DMAPending)
                       {
                        VDC_UNDEFINED("Set LENR while DMAPending: %04x\n", LENR);
                       }*/

                       if(msb)
       	               {
                        //VDC_DEBUG("DMA: %04x %04x %04x, %02x", SOUR, DESR, LENR, DCR);
			DMAPending = 1;
                       }
                       break;

            case 0x13: VDC_REGSETP(DVSSR, V, msb);
		       SATBPending = 1;
		       break;

            default:   //VDC_WARNING("Unknown VDC register write: %04x %02x", select, V);
		       break;
           }
           break;
 }
}


void VDC_Write16(VDC *vdc, bool A, uint16 V)
{
 if(!A)
  select = V & 0x1F;
 else
 {
  switch(select & 0x1F)
  {
            case 0x00: MAWR = V;
		       break;


            case 0x01: MARR = V;

		       VDC_DoWaitStates(vdc);

		       pending_read = TRUE;
		       pending_read_addr = MARR;

	               MARR += vram_inc_tab[(CR >> 11) & 0x3];

		       VDC_CheckAndCommitPending(vdc);
                       break;


            case 0x02: // We must call CommitPendingWrite at the end of SAT/VRAM DMA for this to work!
			VDC_DoWaitStates(vdc);

			pending_write = TRUE;
			pending_write_addr = MAWR;
			pending_write_latch = V;
	                MAWR += vram_inc_tab[(CR >> 11) & 0x3];

			VDC_CheckAndCommitPending(vdc);
                       break;

            case 0x05: CR = V;
                       break;

            case 0x06: RCR = V & 0x3FF;
		       break;

            case 0x07: BXR = V & 0x3FF;
                       //VDC_DEBUG("BXR Set");
                       break;

            case 0x08: BYR = V & 0x1FF;
                       BG_YMoo = BYR;
                       //VDC_DEBUG("BYR Set");
                       break;

            case 0x09: MWR = V; break;
       	    case 0x0a: HSR = V; break;
            case 0x0b: HDR = V; break;
       	    case 0x0c: VSR = V; break;
            case 0x0d: VDR = V; break;
       	    case 0x0e: VCR = V; break;

            case 0x0f: DCR = V;
                       /*if(DMARunning)
                       {
                        VDC_UNDEFINED("Set DCR during DMA: %04x\n", DCR);
                       }

                       if(DMAPending)
                       {
                        VDC_UNDEFINED("Set DCR while DMAPending: %04x\n", DCR);
                       }*/

                       break;

            case 0x10: SOUR = V;
                       /*if(DMARunning)
                       {
                        VDC_UNDEFINED("Set SOUR during DMA: %04x\n", SOUR);
                       }

                       if(DMAPending)
                       {
                        VDC_UNDEFINED("Set SOUR while DMAPending: %04x\n", SOUR);
                       }*/
                       break;

            case 0x11: DESR = V;
                       /*if(DMARunning)
                       {
                        VDC_UNDEFINED("Set DESR during DMA: %04x\n", DESR);
                       }
                       if(DMAPending)
                       {
                        VDC_UNDEFINED("Set DESR while DMAPending: %04x\n", DESR);
                       }*/
                       break;

            case 0x12: LENR = V;
                       /*if(DMARunning)
                       {
                        VDC_UNDEFINED("Set LENR during DMA: %04x\n", LENR);
                       }

                       if(DMAPending)
                       {
                        VDC_UNDEFINED("Set LENR while DMAPending: %04x\n", LENR);
                       }

                       VDC_DEBUG("DMA: %04x %04x %04x, %02x", SOUR, DESR, LENR, DCR);
						*/
		       DMAPending = 1;
                       break;

            case 0x13: DVSSR = V;
		       SATBPending = 1;
		       break;

            default:   //VDC_WARNING("Oops 2: %04x %02x", select, V);
		       break;
  }
 }

}



int32 VDC_Reset(VDC *vdc)
{
 memset(VRAM, 0, sizeof(VRAM));
 memset(SAT, 0, sizeof(SAT));
 memset(SpriteList, 0, sizeof(SpriteList));

 for(uint32 A = 0; A < 65536; A += 16)
  VDC_FixTileCache(vdc, A);

 pending_read = false;
 pending_read_addr = 0xFFFF;
 read_buffer = 0xFFFF;
 write_latch = 0;

 pending_write = false;
 pending_write_addr = 0xFFFF;
 pending_write_latch = 0xFFFF;

 status = 0;

 HSR = 0;
 HDR = 0;
 VSR = 0;
 VDR = 0;
 VCR = 0;

 HSW_cache = M_vdc_HSW;
 HDS_cache = M_vdc_HDS;
 HDW_cache = M_vdc_HDW;
 HDE_cache = M_vdc_HDE;

 VDS_cache = M_vdc_VDS;
 VSW_cache = M_vdc_VSW;
 VDW_cache = M_vdc_VDW;
 VCR_cache = M_vdc_VCR;



 MAWR = 0;
 MARR = 0;

 CR = CR_cache = 0;
 RCR = 0;
 BXR = 0;
 BYR = 0;
 MWR = 0;
 MWR_cache = 0;

 DCR = 0;
 SOUR = 0;
 DESR = 0;
 LENR = 0;
 DVSSR = 0;

 VDMA_CycleCounter = 0;

 RCRCount = 0;

 DMAReadBuffer = 0;
 DMAReadWrite = 0;
 DMARunning = 0;
 DMAPending = 0;
 SATBPending = 0;
 burst_mode = 0;

 BG_XOffset = 0;
 BG_YOffset = 0;
 BG_YMoo = 0;

 sat_dma_counter = 0;
 select = 0;

 pixel_copy_count = 0;


 NeedRCRInc = false;
 NeedVBIRQTest = false;
 NeedSATDMATest = false;
 NeedBGYInc = false;

 HPhase = 0;
 VPhase = 0;
 HPhaseCounter = 1;
 VPhaseCounter = 1;

 sprite_cg_fetch_counter = 0;

 mystery_counter = 0;
 mystery_phase = false;

 pixel_desu = 0;
 pixel_copy_count = 0;
 active_sprites = 0;

 return(VDC_CalcNextEvent(vdc));
}

VDC *VDC_New(bool nospritelimit, uint32 par_VRAM_Size)
{
 VDC *vdc = (VDC *)calloc(1, sizeof(VDC));
 if(!vdc)
  return NULL;
 userle = ~0U;
 unlimited_sprites = nospritelimit;
 VRAM_Size = par_VRAM_Size;
 VRAM_SizeMask = VRAM_Size - 1;
 VRAM_BGTileNoMask = VRAM_SizeMask / 16;
 WSHook = NULL;
 IRQHook = NULL;
 in_exhsync = false;
 in_exvsync = false;
 return vdc;
}

void VDC_Delete(VDC *vdc)
{
 free(vdc);
}

static void VDC_StateExtraPack(VDC *vdc, uint8 *buf)
{
 uint8 *p = buf;
 for(int i = 0; i < 64 * 2; i++)
 {
  SPRLE *sp = &SpriteList[i];
  p[0] = (uint8)(sp->x >> 0); p[1] = (uint8)(sp->x >> 8); p[2] = (uint8)(sp->x >> 16); p[3] = (uint8)(sp->x >> 24); p += 4;
  p[0] = (uint8)(sp->flags >> 0); p[1] = (uint8)(sp->flags >> 8); p[2] = (uint8)(sp->flags >> 16); p[3] = (uint8)(sp->flags >> 24); p += 4;
  *p++ = sp->palette_index;
  for(int pd = 0; pd < 4; pd++) { p[0] = (uint8)(sp->pattern_data[pd] >> 0); p[1] = (uint8)(sp->pattern_data[pd] >> 8); p += 2; }
 }
}

static void VDC_StateExtraUnpack(VDC *vdc, const uint8 *buf)
{
 const uint8 *p = buf;
 for(int i = 0; i < 64 * 2; i++)
 {
  SPRLE *sp = &SpriteList[i];
  sp->x = (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24); p += 4;
  sp->flags = (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24); p += 4;
  sp->palette_index = *p++;
  for(int pd = 0; pd < 4; pd++) { sp->pattern_data[pd] = (uint16)p[0] | ((uint16)p[1] << 8); p += 2; }
 }
}

int VDC_StateAction(VDC *vdc, StateMem *sm, int load, int data_only, const char *sname)
{
 int ret = 1;
 uint8 extra_state[64 * 2 * 17];

 VDC_StateExtraPack(vdc, extra_state);

 SFORMAT StateRegs[] = 
 {
	SFVAR(in_exhsync),
	SFVAR(in_exvsync),

        SFVARN(sat_dma_counter, "sat_dma_counter"),

        SFVARN(select, "select"),
        SFVARN(MAWR, "MAWR"),
        SFVARN(MARR, "MARR"),
        SFVARN(CR, "CR"),
	SFVAR(CR_cache),
        SFVARN(RCR, "RCR"),
        SFVARN(BXR, "BXR"),
        SFVARN(BYR, "BYR"),
        SFVARN(MWR, "MWR"),

        SFVARN(HSR, "HSR"),
        SFVARN(HDR, "HDR"),
        SFVARN(VSR, "VSR"),
        SFVARN(VDR, "VDR"),

        SFVARN(VCR, "VCR"),
        SFVARN(DCR, "DCR"),
        SFVARN(SOUR, "SOUR"),
        SFVARN(DESR, "DESR"),
        SFVARN(LENR, "LENR"),
        SFVARN(DVSSR, "SATB"),


	SFVAR(VDMA_CycleCounter),

        SFVARN(RCRCount, "RCRCount"),


	SFVAR(pending_read),
	SFVAR(pending_read_addr),
        SFVAR(read_buffer),

        SFVAR(write_latch),

	SFVAR(pending_write),
	SFVAR(pending_write_addr),
	SFVAR(pending_write_latch),

        SFVARN(status, "status"),

        SFARRAY16N(SAT, 0x100, "SAT"),

        SFARRAY16N(VRAM, VRAM_Size, "VRAM"),

        SFVARN(DMAReadBuffer, "DMAReadBuffer"),
        SFVARN(DMAReadWrite, "DMAReadWrite"),
        SFVARN(DMARunning, "DMARunning"),
	SFVAR(DMAPending),
        SFVARN(SATBPending, "SATBPending"),
        SFVARN(burst_mode, "burst_mode"),

        SFVARN(BG_YOffset, "BG_YOffset"),
        SFVARN(BG_XOffset, "BG_XOffset"),

	SFVAR(HSW_cache),
	SFVAR(HDS_cache),
	SFVAR(HDW_cache),
	SFVAR(HDE_cache),

	SFVARN(VDS_cache, "VDS_cache"),
        SFVARN(VSW_cache, "VSW_cache"),
        SFVARN(VDW_cache, "VDW_cache"),
        SFVARN(VCR_cache, "VCR_cache"),
	SFVARN(MWR_cache, "MWR_cache"),


	SFVAR(BG_YMoo),
	SFVAR(NeedRCRInc),
	SFVAR(NeedVBIRQTest),
	SFVAR(NeedSATDMATest),
	SFVAR(NeedBGYInc),

	SFVAR(HPhase),
	SFVAR(VPhase),
	SFVAR(HPhaseCounter),
	SFVAR(VPhaseCounter),

	SFVAR(sprite_cg_fetch_counter),

	SFVAR(mystery_counter),
	SFVAR(mystery_phase),

	SFVAR(active_sprites),

	SFARRAYN(extra_state, sizeof(extra_state), "ExtraState"),
	//SFARRAY(SpriteListTemp, sizeof(SpriteListTemp)),

	SFEND
  };

  ret &= MDFNSS_StateAction(sm, load, data_only, StateRegs, sname, false);

  if(load)
  {
   VDC_StateExtraUnpack(vdc, extra_state);

   for(int x = 0; x < VRAM_Size; x++)
    VDC_FixTileCache(vdc, x);
  }

 return(ret);
}
