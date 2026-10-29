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

/* Emulation for HuC6261(descendant of the VCE) and the HuC6272(KING) */
/* Note: Some technical comments may be outdated */

/*
 Current issues:
	VCE "natural" priorities for the layers when their priorities are the same(technically an illegal condition) are probably not correct.  A game test case: "Miraculum" erroneously sets
	up the priority	registers like this after exiting the airship(I believe).

	SCSI RST interrupt timing is guessed(and with nothing to go on), so it definitely needs to be tested on the real thing.

	The data bus is not handled/asserted properly.  Excluding pseudo-DMA and DMA modes(which I'd need to test), the data bus will only be asserted if the lower bit of register 0x1 is set, and the
	phase match bits must match the state of the C/D, I/O, and MSG signals(IE there isn't a bus mismatch state).

	Raw subchannel reading timing is probably wrong.

	KRAM mode page mapping is implemented; K-BUS arbitration and refresh remain approximate.
*/

#ifdef __MMX__
#include <mmintrin.h>
#endif
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "video_blit.h"
#include "cd_xfer_debug.h"
#include "pcfx.h"
#include "king.h"
#include "interrupt.h"
#include "rainbow_huffman_stream.h"
#include "rainbow_mcu_luma.h"
#include "rainbow.h"
#include "soundbox.h"
#include "input.h"
#include "timer.h"
#ifdef HAVE_HUC6273
#include "huc6273.h"
#endif
#include "../cdrom/scsicd.h"
#include "pcfx_yuv_color.h"
#include "../clamp.h"
#include "../state_helpers.h"
#ifdef V810_PROFILE
#include "../hw_cpu/v810/v810_profile.h"
#endif


#ifdef _WIN32
static inline void KINGDBG(const char *format, ...) { (void)0; }
static inline void ADPCMDBG(const char *format, ...) { (void)0; }
#else
#define KINGDBG(format, ...) (void)0
#define ADPCMDBG(format, ...) (void)0
#endif
//#define KINGDBG FXDBG
#define KING_UNDEF FXDBG
//FXDBG

/*
 SCSI Questions(this list needs to be revised more and merged into the issues list at the beginning of the file):

  What happens when there is no more data to transfer during DMA and the status SCSI bus phase is entered(before the DMA count reaches 0)?

  Why is the "sequential DMA" bit needed?

  Which SCSI registers return the values of the SCSI bus, and which return latched values(from previous writes or pseudo-DMA)?

  Is real DMA layered on top of pseudo-DMA?  Reading the developer documents, it looks that way.

  What triggers the setting of ACK during pseudo-DMA?  A timer?  Reading from the upper 16-bits of KING register 0x05?  The lower bits(in which case,
  the value would be latched..)?

*/

// 16 bit YUV format:  upper 8 bits Y, next 4 bits U, lower 4 bits V, transformed to 8-bit U and 8-bit V by shifting in 0 in lower bits.

typedef struct
{
 uint8 AR;

 uint16 priority[2];	/* uint16 0:
			   	bit   3-0: Legacy VDC BG priority?
				bit   7-4: Legacy VDC SPR priority?
				bit  11-8: RAINBOW(MJPEG) priority
			   uint16 1:
				bit   3-0: KING BG0 priority
				bit   7-4: KING BG1 priority
				bit  11-8: KING BG2 priority
				bit 15-12: KING BG3 priority
			*/

 bool odd_field;	/* TRUE if interlaced mode is enabled and we're in the odd field, FALSE otherwise. */

 bool in_hblank;	/* TRUE if we're in H-blank */
 bool in_vdc_hsync;

 uint16 picture_mode;         // CPU-visible CR, written immediately
 uint16 active_picture_mode;  // CR after separate V/H-period latches

 bool dot_clock;	 // Cached from picture_mode in hblank
 uint32 dot_clock_ratio; // Cached from picture mode in hblank
 int32 clock_divider;

 int32 vdc_event[2];


 uint32 raster_counter;

 uint16 palette_rw_offset; // Read/write offset
 uint16 palette_rw_latch;

 uint16 palette_offset[4]; // 
			   // BMG1 and BMG 0 in [1](BMG1 in upper 8 bits, BMG0 in lower), BMG2 and 3 in [2]
			   // RAINBOW in lower(?) 8 bits of [3]?

 uint16 palette_table[512]; // The YUV palette, woohoo!

 // Chroma keys, minimum value in lower 8 bits, maximum value in upper 8 bits
 uint16 ChromaKeyY; // Register 0xA
 uint16 ChromaKeyU; // Register 0xB
 uint16 ChromaKeyV; // register 0xC

 uint16 CCR; // Register 0x0D, fixed color register, 16-bit YUV
 uint16 BLE; // Register 0x0E, cellophane setting register
	     //  15   14   13   12   11   10   09   08   07   06   05   04   03   02   01   00
	     // F/B |E/D |   IDU   |CTRL BMG3|CTRL BMG2|CTRL BMG1|CTRL BMG0|  VDP SP | VDP BG
	     // IDU is AKA Rainbow
	     // F/B
	     //	 0 = Back cellophane.
	     //  1 = Front cellophane.
	     // E/D
	     //  0 = Back/Front cellophane off
	     //  1 = Back/Front cellophane on
	     // Ctrl format:
	     //  00 = Cellophane disabled.
	     //  01 = Cellophane with 1A/1B(Coeffs 0, 1?)
	     //  02 = Cellophane with 2A/2B(Coeffs 2, 3?)
	     //  03 = Cellophane with 3A/3B(Coeffs 4, 5?)

 uint16 SPBL; // Register 0x0F, Sprite cellophane setting register
	      // Specifies a color palette bank for which cellophane will always be disabled.

 uint16 coefficients[6]; // Cellophane coefficients, YUV, 4-bits each, on the least-significant end(xxxxYYYYUUUUVVVV).
			 // Valid settings: 0(cellophane disabled for layer), 1-8.  9-F are "unsupported".
} fx_vce_t;

fx_vce_t fx_vce;

/* Documented HuC6261 reset defaults (C6261 §2.11). */
static inline void VCE_ResetRegisters(void)
{
 memset(&fx_vce, 0, sizeof(fx_vce));
 fx_vce.SPBL = 0xFFFFu;
}

//
// VCE render cache, including registers cached at hblank
//
typedef struct
{
 uint16 priority[2];
 uint16 picture_mode;

 uint16 palette_offset[4];
 uint32 palette_table_cache[512 * 2]; // 24-bit YUV cache for SPEED(HAH), * 2 to remove need for & 0x1FF in rendering code

 uint16 ChromaKeyY;
 uint16 ChromaKeyU;
 uint16 ChromaKeyV;

 uint16 CCR;
 uint16 BLE;

 uint16 SPBL;

 uint16 coefficients[6];

 uint8 coefficient_mul_table_y[16][256];
 int8 coefficient_mul_table_uv[16][256];

 uint32 LayerPriority[8];	// [LAYER_n] = ordered_priority_for_n(real priority 0-15 mapped to 1-7)
				//	       priority = 0, layer is disabled(via the layer enable bit not being set)
} vce_rendercache_t;

static vce_rendercache_t vce_rendercache;

static int32 scsicd_ne;

enum
{
 HPHASE_ACTIVE = 0,
 HPHASE_HBLANK_PART1,
 HPHASE_HBLANK_PART3,
 HPHASE_HBLANK_PART4,
 HPHASE_COUNT
};

static int32 HPhase;
static int32 HPhaseCounter;
static int32 vdc_lb_pos;
static uint8 vce_state_latch_version = 1;
/* C6261 §2.10 does not establish an absolute mixer-to-VDC phase.
   Keep Mednafen's historical 18-tick offset as default, but expose it for
   physical-console calibration. Never alter the 3:4 clock relation. */
static int vdc_hsync_phase_ticks = 18;
void KING_SetVDCHSyncPhase(int ticks)
{
 if(ticks < -64) ticks = -64;
 if(ticks > 64) ticks = 64;
 vdc_hsync_phase_ticks = ticks;
}
int KING_GetVDCHSyncPhase(void) { return vdc_hsync_phase_ticks; }

static MDFN_ALIGN(8) uint16 vdc_linebuffers[2][512];
static MDFN_ALIGN(8) uint32 vdc_linebuffer[512];
static MDFN_ALIGN(8) uint32 vdc_linebuffer_yuved[512];
static MDFN_ALIGN(8) uint32 rainbow_linebuffer[256];

// 8 * 2 for left + right padding for scrolling
static MDFN_ALIGN(8) uint32 bg_linebuffer[256 + 8 + 8];
#ifdef HAVE_HUC6273
static MDFN_ALIGN(8) uint8 huc6273_vce_top_prio_line[1024];
#endif



// Don't change these enums, there are some hardcoded values still used(particularly, LAYER_NONE).
enum
{
 LAYER_NONE = 0,
 LAYER_BG0,
 LAYER_BG1,
 LAYER_BG2,
 LAYER_BG3,
 LAYER_VDC_BG,
 LAYER_VDC_SPR,
 LAYER_RAINBOW
};

static uint8 VCEPrioMap[8][8][8][4];	// [n][n][n][3] is dummy, for padding to a power of 2.

static void BuildCMT(void)
{
 for(int coeff = 0; coeff < 16; coeff++)
 {
  for(int value = 0; value < 256; value++)
  {
   vce_rendercache.coefficient_mul_table_y[coeff][value] = (value * coeff / 8); // Y
   vce_rendercache.coefficient_mul_table_uv[coeff][value] = ((value - 128) * coeff / 8); // UV
  }
 }

}

static inline void RebuildLayerPrioCache(void)
{
 vce_rendercache_t *vr = &vce_rendercache;

 vr->LayerPriority[LAYER_NONE] = 0;

 for(int n = 0; n < 4; n++)
 {
  if(((vce_rendercache.picture_mode >> (10 + n)) & 1))
  {
   vr->LayerPriority[LAYER_BG0 + n] = (((vce_rendercache.priority[1] >> (n * 4)) & 0xF) + 1);
   if(vr->LayerPriority[LAYER_BG0 + n] > 8)
   {
    //printf("KING BG%d Priority Too Large: %d\n", n, vr->LayerPriority[LAYER_BG0 + n] - 1);
    vr->LayerPriority[LAYER_BG0 + n] = 0;
   }
  }
  else
   vr->LayerPriority[LAYER_BG0 + n] = 0;
 }

 if(vce_rendercache.picture_mode & 0x0100)
 {
  vr->LayerPriority[LAYER_VDC_BG] = ((vce_rendercache.priority[0] & 0xF) + 1);
  if(vr->LayerPriority[LAYER_VDC_BG] > 8)
  {
   //printf("VDC BG Priority Too Large: %d\n", vr->LayerPriority[LAYER_VDC_BG] - 1);
   vr->LayerPriority[LAYER_VDC_BG] = 0;
  }
 }
 else
  vr->LayerPriority[LAYER_VDC_BG] = 0;

 if(vce_rendercache.picture_mode & 0x0200)
 {
  vr->LayerPriority[LAYER_VDC_SPR] = (((vce_rendercache.priority[0] >> 4) & 0xF) + 1);
  if(vr->LayerPriority[LAYER_VDC_SPR] > 8)
  {
   //printf("VDC SPR Priority Too Large: %d\n", vr->LayerPriority[LAYER_VDC_SPR] - 1);
   vr->LayerPriority[LAYER_VDC_SPR] = 0;
  }
 }
 else 
  vr->LayerPriority[LAYER_VDC_SPR] = 0;

 if(vce_rendercache.picture_mode & 0x4000)
 {
  vr->LayerPriority[LAYER_RAINBOW] = (((vce_rendercache.priority[0] >> 8) & 0xF) + 1);
  if(vr->LayerPriority[LAYER_RAINBOW] > 8)
  {
   //printf("RAINBOW Priority Too Large: %d\n", vr->LayerPriority[LAYER_RAINBOW] - 1);
   vr->LayerPriority[LAYER_RAINBOW] = 0;
  }
 }
 else
  vr->LayerPriority[LAYER_RAINBOW] = 0;


 // At this point, all entries in vr->LayerPriority should be one of 0 through 8(inclusive).

 int RemapPriority = 1;
 bool Done[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

 for(unsigned int i = 1; i < (1 + 8); i++)
 {
  for(uint_fast8_t n = 0; n < 4; n++)
  {
   if(vr->LayerPriority[LAYER_BG0 + n] == i && !Done[LAYER_BG0 + n])
   {
    vr->LayerPriority[LAYER_BG0 + n] = RemapPriority++;
    Done[LAYER_BG0 + n] = true;
   }
  }

  if(vr->LayerPriority[LAYER_VDC_BG] == i && !Done[LAYER_VDC_BG])
  {
   vr->LayerPriority[LAYER_VDC_BG] = RemapPriority++;
   Done[LAYER_VDC_BG] = true;
  }

  if(vr->LayerPriority[LAYER_VDC_SPR] == i && !Done[LAYER_VDC_SPR])
  {
   vr->LayerPriority[LAYER_VDC_SPR] = RemapPriority++;
   Done[LAYER_VDC_SPR] = true;
  }

  if(vr->LayerPriority[LAYER_RAINBOW] == i && !Done[LAYER_RAINBOW])
  {
   vr->LayerPriority[LAYER_RAINBOW] = RemapPriority++;
   Done[LAYER_RAINBOW] = true;
  }
 }
 //assert(RemapPriority <= 8);

 //if(fx_vce.raster_counter == 50)
 // MDFN_DispMessage("%d BG0: %d %d %d %d, VBG: %d, VSPR: %d, RAIN: %d", vr->LayerPriority[0], vr->LayerPriority[1], vr->LayerPriority[2], vr->LayerPriority[3],
 //						vr->LayerPriority[4], vr->LayerPriority[5], vr->LayerPriority[6], vr->LayerPriority[7]);
}

// Call this function in FX VCE hblank(or at the end/immediate start of active display)
static void DoHBlankVCECaching(void)
{
 const fx_vce_t *source = &fx_vce;
 vce_rendercache_t *dest = &vce_rendercache;

 /* C6261 R00: bits 8..14 take effect in the NEXT horizontal period.
    The scan, clock, colour and sync bits take effect only at V boundary.
    Preserve the CPU-readable write register separately from the signal. */
 fx_vce.active_picture_mode = (fx_vce.active_picture_mode & 0x80FFu) |
                              (fx_vce.picture_mode & 0x7F00u);
 dest->picture_mode = fx_vce.active_picture_mode;

 fx_vce.dot_clock = (bool)(fx_vce.active_picture_mode & 0x08);
 fx_vce.dot_clock_ratio = fx_vce.dot_clock ? 3 : 4;

 for(uint_fast8_t i = 0; i < 2; i++)
  dest->priority[i] = source->priority[i];
 

 for(uint_fast8_t i = 0; i < 4; i++)
  dest->palette_offset[i] = source->palette_offset[i];

 dest->ChromaKeyY = source->ChromaKeyY;
 dest->ChromaKeyU = source->ChromaKeyU;
 dest->ChromaKeyV = source->ChromaKeyV;

 dest->CCR = source->CCR;
 dest->BLE = source->BLE;
 dest->SPBL = source->SPBL;

 for(uint_fast8_t i = 0; i < 6; i++)
  dest->coefficients[i] = source->coefficients[i];

 RebuildLayerPrioCache();
}

/* Called at the field boundary, BEFORE the next field is clocked. The
   output mode remains fixed for the current field even if software writes CR. */
static inline void VCE_LatchVerticalControl(void)
{
 fx_vce.active_picture_mode = (fx_vce.active_picture_mode & 0x7F00u) |
                              (fx_vce.picture_mode & 0x80FFu);
}

static inline void RedoPaletteCache(int n)
{
 uint32 YUV = fx_vce.palette_table[n];
 uint8 Y = (YUV >> 8) & 0xFF;
 uint8 U = (YUV & 0xF0);
 uint8 V = (YUV & 0x0F) << 4;

 vce_rendercache.palette_table_cache[n] = 
 vce_rendercache.palette_table_cache[0x200 | n] = (Y << 16) | (U << 8) | (V << 0);
}

enum
{
 BGMODE_INVALID = 0,
 BGMODE_4 = 1,
 BGMODE_16 = 2,
 BGMODE_256 = 3,
 BGMODE_64K = 4,
 BGMODE_16M = 5,
 BGMODE_64K_EXTDOT = 6,
 BGMODE_16M_EXTDOT = 7,
};

typedef struct
{
	uint8 AR;
	
	uint32 KRAMRA, KRAMWA;
	uint8 KRAM_Mode;

	uint32 PageSetting;
        uint16 *RainbowPagePtr, *DMAPagePtr;    // Calculated off of PageSetting

	uint16 bgmode; // 4 bits each BG: 3333 2222 1111 0000
		       /* Possible settings:
				0x0:  Invalid?
				0x1:  4-color palette, 1 byte for 4 pixels, transparent on entry 0
				0x2:  16-color palette, 1 byte for 2 pixels, transparent on entry 0
				0x3:  256-color palette, 1 byte for 1 pixel, transparent on entry 0
				0x4:  64K color(Y-8, U-4, V-4), 1 halfword for 1 pixel, transparent on Y=0
				0x5:  16M colors(Y-8, Y-8, U-8, V-8, 4 bytes for 2 pixels), transparent on Y=0

				If & 8, enable palette bank mode(only for 4 and 16-colors)???
					BAT format would be PPPPCCCCCCCCCCCC in this mode.
					4 color: 00PPPPnn 16 color: PPPPnnnn, where "n" is the 2 or 4-bit pixel data
			*/
	uint16 priority;

	uint16 BGScrollMode;	// Register 0x16
	uint16 BGSize[4];

	uint8 BGBATAddr[4];
	uint8 BGCGAddr[4];
        uint8 BG0SubBATAddr, BG0SubCGAddr;
	bool BG0FetchRehomed;

	uint16 BGXScroll[4];
	uint16 BGYScroll[4];
	
        uint16 BGXScrollCache[4];
        uint16 BGYScrollCache[4];

	uint16 BGAffinA, BGAffinB, BGAffinC, BGAffinD;
	uint16 BGAffinCenterX, BGAffinCenterY;

	uint16 ADPCMControl; // effective: READ EN only latches ON at -HSYNC
    uint16 ADPCMControlWritten; // last CPU write to R50
    uint8 ADPCMPendingEnable; // channel enables waiting for next falling -HSYNC
    uint8 ADPCMTransferLatchVersion;
	uint16 ADPCMBufferMode[2];

	uint16 ADPCMSAL[2];

	uint32 ADPCMEndAddress[2];
	uint32 ADPCMPlayAddress[2];
	uint16 ADPCMIntermediateAddress[2];
	uint16 ADPCMStatus[2]; // Register 0x53, a bit maimed :)
	bool ADPCMIRQPending;

	uint16 RAINBOWTransferControl; // Register 0x40
	uint32 RAINBOWKRAMA;	       // Register 0x41
        uint16 RAINBOWTransferStartPosition; // Register 0x42, line number(0-262)
	uint16 RAINBOWTransferBlockCount; // Register 0x43 (CPU-written)

    /* C6272_2 3.4: R41 and R43 are I* (deferred while a 16H
       compressed-image block is in flight). These active copies, unlike
       the CPU-side registers, only update at the next block boundary. */
    uint32 RAINBOWActiveKRAMA;
    uint16 RAINBOWActiveBlockCount;
    uint8 RAINBOWConfigPending; // bit 0: R41; bit 1: R43
    uint8 RAINBOWConfigStateVersion;

	bool RAINBOWStartPending;
	int32 RAINBOWBusyCount, RAINBOWBlockCount;

	uint16 RasterIRQLine; // Register 0x44
	bool RasterIRQPending;

	uint32 RAINBOWKRAMReadPos;
        /* HuC6272 §3.4.2: KING reads beyond -REQR deassertion.  The
         * documented three guard words are represented as grant-time
         * snapshots, NOT as speculative accesses by the synchronous decoder.
         * This is a partial prefetch model, not an asynchronous HuC6271. */
        uint32 RainbowAheadAddress[3];
        uint32 RainbowAheadGrantAt[3];
        uint16 RainbowAheadValue[3];
        uint8 RainbowAheadPage[3];
        uint8 RainbowAheadStatus[3]; /* 0 empty, 1 requested, 2 delivered */
        uint8 RainbowAheadStateVersion;

        /* v19: asynchronous HuC6271 compressed-input staging. KRAM bytes are
         * sampled only after actual modeled grants, not by the JPEG callback.
         * Parsing waits for a complete declared block before feeding the
         * existing entropy/RLE backends. This is a block-atomic first stage,
         * NOT a cycle-accurate resumable Huffman engine. */
        uint8 RBAsyncStage[131072];
        uint32 RBAsyncLength, RBAsyncProducerPos, RBAsyncConsumeIndex;
        uint32 RBAsyncScanIndex, RBAsyncTarget, RBAsyncGrantAt;
        uint32 RBAsyncHeaderRemaining, RBAsyncSkipRemaining;
        uint32 RBAsyncWords, RBAsyncWaits, RBAsyncUnderflows;
        uint8 RBAsyncStageVersion, RBAsyncActive, RBAsyncPending, RBAsyncDecoding;
        uint8 RBAsyncFirst, RBAsyncSkip, RBAsyncScanState, RBAsyncMarker;
        uint8 RBAsyncSizeHigh, RBAsyncPage;
        uint32 RBAsyncAddress;
        /* v20: RLE token cursor and pixel location survive grant stalls and
         * mid-block save/load.  The actual decoder output lives in RAINBOW. */
        uint32 RBAsyncRLECursor, RBAsyncRLEEnd, RBAsyncRLEX;
        uint32 RBAsyncRLETokenCount, RBAsyncRLETotalTokens, RBAsyncRLEBlocks;
        uint8 RBAsyncRLEActive, RBAsyncRLEVersion;
        /* Grant-fed F8/FF quantizer and stuffed-entropy stream. This parser
         * preserves byte boundaries, but Huffman/IDCT remains block-atomic. */
        uint8 RBJPEGVersion, RBJPEGActive, RBJPEGQuantExpected, RBJPEGQuantCount;
        uint8 RBJPEGQuant[128];
        uint32 RBJPEGCursor, RBJPEGEntropyEnd, RBJPEGEntropyBytes;
        uint32 RBJPEGStuffedPairs, RBJPEGStalledPairs, RBJPEGBlocks;
        uint32 RBJPEGDigest;
        /* v22: grant-fed, transactional JPEG Huffman coefficient tracker.
         * Parsing state is serialised, but the existing IDCT backend still
         * renders the completed block. No guesses about silicon FIFO size. */
        RBH_State RBH;
        uint8 RBHVersion, RBHStarted;
        uint32 RBHCompletedBlocks;
        /* v23 diagnostic IDCT: stores completed 16x16 luma columns until
         * the legacy backend finishes rendering the corresponding strip. */
        uint8 RBMCULuma[4096], RBMCUValidColumn[16], RBMCUQuantBase[64];
        uint8 RBMCUQuantValid, RBMCUScale, RBMCUVersion;
        uint32 RBMCUColumns, RBMCUCompared, RBMCUDifferences, RBMCUBlocks, RBMCUExactBlocks;



	bool DMATransferFlipFlop;
	uint32 DMATransferAddr; // Register 0x09
	uint32 DMATransferSize; // Register 0x0A
	uint16 DMAStatus;	// Register 0x0B
	uint8 DMALatch;

	/* HYPOTHESIS, not documented silicon behaviour -- see docs/king-dma-erratum.md
	 * for the evidence, the reasoning and how to falsify it.  Enabled by the
	 * PCFX_KING_DMA_ERRATUM env var (see king_dma_erratum_enabled()).
	 *
	 * Model: the real-DMA engine does not retire when the programmed transfer
	 * count reaches zero; it retires one REQ/ACK handshake later, and that
	 * trailing item is discarded rather than written to KRAM.  Consequences:
	 *
	 *   - A request SHORTER than the sector the drive actually delivers leaves
	 *     leftover bytes in DATA IN, so the trailing handshake happens and the
	 *     DMA retires (data intact, since the extra byte is discarded).
	 *   - A request of EXACTLY the delivered length leaves nothing over: the
	 *     target drops to STATUS, the pump below stops (it only runs in DATA
	 *     IN), the trailing handshake never arrives, and the DMA never retires.
	 *     The transfer count (reg 0x0A) then never reaches zero -- and that is
	 *     what libpcfx's eris_scsi_check_dma() polls -- so the driver wedges.
	 *
	 * TRUE between "count would hit zero" and "trailing item consumed". */
	bool DMARetirePending;

	/* The trailing-item wedge only applies to a COUNT-BOUNDED transfer: one
	 * programmed with a nonzero word count (reg 0x0A) that the drive then
	 * exactly satisfies.  A transfer started with count 0 is phase-driven --
	 * it runs until the target leaves DATA IN, and its count register only
	 * reaches zero by wrapping the full 18-bit range after every delivered
	 * byte has already landed.  That wraparound is completion, not an
	 * exact-length stall, so such a transfer must retire normally (this is
	 * what Team Innocent's CD loads do; without this they wedge like doom).
	 * Captured at DMA start (reg 0x07) from the then-current count. */
	bool DMACountBounded;

	/* --- CPU-PIO SCSI read failure model (king_pio_erratum_enabled) ---------
	 * doom-pcfx loads its ADPCM SFX bank with a CPU-driven PIO read (libpcfx
	 * eris_cd_read_kram_pio: the CPU manually reads the SCSI data register and
	 * toggles ACK, NOT the KING real-DMA engine), and on real hardware that
	 * read FAILS -- doom's first CD read returns 0, I_Error fires, and the
	 * boot bar oscillates forever (pcfx_fatal_blink).  Measured on hardware
	 * (probe 027): CPU-PIO reads succeed only intermittently and the driver's
	 * retry does not recover.
	 *
	 * The discriminator has to be the TRANSFER METHOD (CPU-PIO vs KING real-
	 * DMA), not "any CD-data read issued while a fast timer IRQ is hot" --
	 * DMA is hardware-paced and immune on real silicon (retail titles do DMA
	 * reads under hot timers and work fine).  So this can't be decided before
	 * the first sector ever reaches DATA_IN -- at that point the transfer
	 * method isn't known yet, dma_receive_active is FALSE for every read
	 * (PIO or DMA) because DMA only arms itself from the driver's phase
	 * observation callback, which fires *after* DATA_IN begins.  Deciding
	 * up front makes the check vacuous: it can never exclude a DMA read.
	 *
	 * Instead: let DATA_IN begin normally for every read, and latch PIOReadSeen
	 * when the CPU itself reads the SCSI data register (KING_Read16 case 0x00)
	 * while dma_receive_active is FALSE -- the same structural signal a real
	 * KING real-DMA pump never produces (it drains the FIFO with its own
	 * hardware handshake, never through the CPU's I/O-mapped register read). */
	bool PIOReadSeen;

	/* What the failure IS, per the real-hardware evidence (probe 027 + the
	 * DoomPCFX_VID_20260719 frame timeline): the first CPU-PIO transfer under
	 * a hot timer POISONS THE BUS.  The failing read dies essentially
	 * instantly (probe 027: a whole failed 8-attempt driver retry loop fits
	 * inside one vblank field, fld=0), every subsequent command -- READ,
	 * SEEK, even REQUEST SENSE -- also fails instantly (rty == fails*7:
	 * once one attempt fails, all remaining attempts fail; per-attempt
	 * recovery never helps), and only a FULL SCSI bus reset restores the
	 * bus (027's one recovery finding, and what libpcfx's fixed retry loop
	 * now does between attempts).
	 *
	 * So the model is a persistent wedge, not a per-command error status:
	 * when a sector boundary is reached with PIOReadSeen + hot timer
	 * (KING_PIOTransferShouldFail()), scsicd.c sets this flag and silently
	 * drops the bus to BUS FREE (a disconnect -- NOT CHECK CONDITION, which
	 * would send the driver into its REQUEST SENSE + SEEK-reposition retry
	 * mill at ~seconds per failed read; the hardware timeline shows
	 * milliseconds).  While set, scsicd.c's command dispatch accepts each
	 * new CDB and then drops it the same way, so retries fail as fast as
	 * the hardware's do (the driver's handshake waits all bail immediately
	 * on BSY-drop/BUS FREE).  Cleared ONLY by a SCSI bus reset (RST) --
	 * matching 027 -- and by power-on/KING reset.  See
	 * KING_PIOBusWedgedNow()/KING_PIOWedgeBus() and
	 * docs/king-pio-read-erratum.md. */
	bool PIOBusWedged;


	uint16 MPROGControl;    // register 0x15
	uint16 MPROGControlCache;
	uint16 MPROGAddress;
	uint16 MPROGData[0x10];
	v810_timestamp_t KRAMWriteBusyUntil;
        /* v13: optional single-entry posted CPU KRAM write: address and data
         * remain invisible to KRAM consumers until the modeled drain time. */
        uint8 KRAMWritePending;
        uint8 KRAMWritePendingPage;
        uint16 KRAMWritePendingValue;
        uint32 KRAMWritePendingAddress;
        v810_timestamp_t KRAMWritePendingAt;
        uint8 KRAMWritePendingStateVersion;
        /* K-BUS requester reservations on each independently serviced bank.
         * Time tags are V810 timestamps aligned to KING dots (4 clocks).
         * Source value zero means empty; see docs/KBUS_REQUESTS_V15.md. */
#define KING_KBUS_SLOTS 1024
        uint32 KBusSlotTime[2][KING_KBUS_SLOTS];
        uint8 KBusSlotSource[2][KING_KBUS_SLOTS];
        uint8 KBusStateVersion;
        /* v16: one transaction per accepted SCSI DMA halfword. The SCSI
         * handshake can finish before the granted KRAM bus write becomes
         * visible. Reserve tags follow priority displacement. */
#define KING_SCSI_PENDING_MAX 128
        uint32 SCSIPendingAddress[KING_SCSI_PENDING_MAX];
        uint32 SCSIPendingGrantAt[KING_SCSI_PENDING_MAX];
        uint16 SCSIPendingValue[KING_SCSI_PENDING_MAX];
        uint8 SCSIPendingPage[KING_SCSI_PENDING_MAX];
        uint8 SCSIPendingValid[KING_SCSI_PENDING_MAX];
        uint16 KBusSlotTag[2][KING_KBUS_SLOTS];
        uint8 SCSIPendingStateVersion;
        uint16 SCSIPendingCount;
        /* HuC6230 receives KRAM words after the ADPCM bus grant, not at
         * the -HSYNC request time. One in-flight request per channel. */
        uint32 ADPCMGrantAddress[2];
        uint32 ADPCMGrantAt[2];
        uint8 ADPCMGrantPage[2];
        uint8 ADPCMGrantPending[2];
        uint8 ADPCMGrantStateVersion;


	bool DMAInterrupt;
	uint8 Reg00;
	uint8 Reg01;
	uint8 Reg02;
	uint8 Reg03;


	uint8 SubChannelControl;

	bool CDInterrupt, SubChannelInterrupt;
	uint8 SubChannelBuf;
	uint8 data_cache;

	bool DRQ;
	bool dma_receive_active;
	bool dma_send_active;
	int32 dma_cycle_counter;
	int32 lastts;


        uint16 KRAM[2][262144];

	#define KING_MAGIC_INTERVAL 10 //4 //32 //10
} king_t;

static king_t *king = NULL;

/* ==================================================================
 * KING (HuC6272) profiler — enabled with -DV810_PROFILE.
 * Register names/numbers mirror the 1995 Game Maker devkit DEV72.H.
 * Measures: KING register R/W traffic (by official name), KRAM
 * bandwidth per engine (CPU port / SCSI-DMA / ADPCM / RAINBOW), and a
 * 4K-word KRAM contention map that auto-flags regions the CPU writes
 * while an engine reads them (the classic "sky/ADPCM garble" bug).
 * ================================================================== */
#ifdef V810_PROFILE
#include <stdio.h>
#define KP_NBUCK  256                      /* 262144 words / 1024 = 256 buckets per page */
#define KP_BSHIFT 10                        /* 1024-word buckets */
/* engines: 0=CPU-wr 1=CPU-rd 2=SCSI-DMA-wr 3=ADPCM-rd 4=RAINBOW-rd 5=BG-rd(estimated) */
#define KP_NENG 6
static const char *const kp_eng_name[KP_NENG] =
	{ "CPU-write", "CPU-read", "SCSI-DMA-wr", "ADPCM-read", "RAINBOW-read", "BG-read(est)" };
static const int kp_eng_is_read[KP_NENG] = { 0, 1, 0, 1, 1, 1 };
static unsigned long long kp_regw[128], kp_regr[128];
static unsigned long long kp_eng_cnt[KP_NENG];               /* total words per engine */
static unsigned long long kp_buck[KP_NENG][2][KP_NBUCK];     /* [engine][page][bucket] */
static uint32_t kp_lo[KP_NENG][2], kp_hi[KP_NENG][2];        /* word-address range hit */
static int kp_rng_init;

static inline void kp_hitw(int eng, int pg, uint32_t ad, unsigned w)
{
	pg &= 1; ad &= 0x3FFFF;
	kp_eng_cnt[eng] += w;
	kp_buck[eng][pg][(ad >> KP_BSHIFT) & (KP_NBUCK - 1)] += w;
	if(!kp_rng_init) { for(int e = 0; e < KP_NENG; e++) for(int p = 0; p < 2; p++)
		{ kp_lo[e][p] = 0xFFFFFFFF; kp_hi[e][p] = 0; } kp_rng_init = 1; }
	if(ad < kp_lo[eng][pg]) kp_lo[eng][pg] = ad;
	if(ad > kp_hi[eng][pg]) kp_hi[eng][pg] = ad;
}
static inline void kp_hit(int eng, int pg, uint32_t ad) { kp_hitw(eng, pg, ad, 1); }

/* BG reads KRAM per-scanline-layer at per-pixel granularity in the blit inner loops;
 * hooking every CG word would bloat the hot path, so we record the layer's BAT+CG
 * footprint once per rendered scanline with a bpp-derived word estimate. */
static inline void kp_bg_scanline(int pg, uint32_t bat_off, uint32_t cg_off, unsigned bgmode)
{
	unsigned bpp7 = bgmode & 0x7;
	unsigned cgw_per_col = (bpp7 >= 4) ? 4 : (bpp7 >= 2 ? 2 : 1);   /* words per 8px tile row */
	kp_hitw(5, pg, bat_off, 34);                 /* ~one BAT row of 34 columns */
	kp_hitw(5, pg, cg_off, 34 * cgw_per_col);    /* ~34 tile-row fetches */
}

/* --- EXACT same-field collision detector ---
 * Per-word generation stamps (no per-frame clear): a word is "touched this field"
 * iff its stamp == kx_gen. If the CPU writes a word an engine (ADPCM/RAINBOW) reads
 * in the SAME field, or vice-versa, that is a real read/write hazard on shared KRAM.
 * CPU-read (self) and BG (estimated, not per-word) are excluded. ~2 MB of stamps. */
static uint16_t kx_wr[2][262144];   /* field-stamp of last CPU write per word */
static uint16_t kx_rd[2][262144];   /* field-stamp of last engine read per word */
static uint8_t  kx_rdeng[2][262144]; /* which engine last read (3=ADPCM,4=RAINBOW) */
static uint16_t kx_gen = 1;
static unsigned long long kx_cpu_then_read, kx_read_then_cpu;
static uint32_t kx_sample_addr[8]; static int kx_sample_eng[8], kx_sample_pg[8]; static int kx_nsample;

static inline void kx_note_sample(int pg, uint32_t ad, int eng)
{
	if(kx_nsample < 8) { kx_sample_pg[kx_nsample] = pg; kx_sample_addr[kx_nsample] = ad;
		kx_sample_eng[kx_nsample] = eng; kx_nsample++; }
}
static inline void kx_write(int pg, uint32_t ad)         /* CPU writes word */
{
	pg &= 1; ad &= 0x3FFFF;
	if(kx_rd[pg][ad] == kx_gen) { kx_cpu_then_read++; kx_note_sample(pg, ad, kx_rdeng[pg][ad]); }
	kx_wr[pg][ad] = kx_gen;
}
static inline void kx_read(int pg, uint32_t ad, int eng) /* ADPCM/RAINBOW reads word */
{
	pg &= 1; ad &= 0x3FFFF;
	if(kx_wr[pg][ad] == kx_gen) { kx_read_then_cpu++; kx_note_sample(pg, ad, eng); }
	kx_rd[pg][ad] = kx_gen; kx_rdeng[pg][ad] = (uint8_t)eng;
}

void king_prof_newfield(void)   /* called once per field from core.c */
{
	if(++kx_gen == 0) { memset(kx_wr, 0, sizeof(kx_wr)); memset(kx_rd, 0, sizeof(kx_rd)); kx_gen = 1; }
}

static const char *king_reg_name(unsigned r)
{
	switch(r & 0x7F) {
	case 0x00: return "SCSI_ODATA/CDATA"; case 0x01: return "SCSI_ICMD";
	case 0x02: return "SCSI_MODE";   case 0x03: return "SCSI_TCMD";
	case 0x04: return "SCSI_SELECT/CBSTAT"; case 0x05: return "SCSI_DMASEND/BUSSTAT";
	case 0x06: return "SCSI_TDMASEND/IDATA"; case 0x07: return "SCSI_DMARCV/IRQRESET";
	case 0x08: return "CDROM_SUBCTRL/DATA"; case 0x09: return "SCSI_DMASTART";
	case 0x0a: return "SCSI_DMACNT";  case 0x0b: return "SCSI_DMACR";
	case 0x0c: return "KRAM_ARR(rdadr)"; case 0x0d: return "KRAM_AWR(wradr)";
	case 0x0e: return "KRAM_DATA";    case 0x0f: return "KRAM_PAGE";
	case 0x10: return "BG_MODE";      case 0x12: return "BG_PRI";
	case 0x13: return "BG_MPAR";      case 0x14: return "BG_MPDR";
	case 0x15: return "BG_MPCR";      case 0x16: return "BG_SSCR";
	case 0x20: return "BG0_BAT";      case 0x21: return "BG0_CG";
	case 0x22: return "BG0_SBAT";     case 0x23: return "BG0_SCG";
	case 0x24: return "BG1_BAT";      case 0x25: return "BG1_CG";
	case 0x28: return "BG2_BAT";      case 0x29: return "BG2_CG";
	case 0x2a: return "BG3_BAT";      case 0x2b: return "BG3_CG";
	case 0x2c: return "BG0_SIZE";     case 0x2d: return "BG1_SIZE";
	case 0x2e: return "BG2_SIZE";     case 0x2f: return "BG3_SIZE";
	case 0x30: return "BG0_BSX";      case 0x31: return "BG0_BSY";
	case 0x32: return "BG1_BSX";      case 0x33: return "BG1_BSY";
	case 0x34: return "BG2_BSX";      case 0x35: return "BG2_BSY";
	case 0x36: return "BG3_BSX";      case 0x37: return "BG3_BSY";
	case 0x38: return "BG_AFINA";     case 0x39: return "BG_AFINB";
	case 0x3a: return "BG_AFINC";     case 0x3b: return "BG_AFIND";
	case 0x3c: return "BG_AFINX";     case 0x3d: return "BG_AFINY";
	case 0x40: return "KR_CR(rainbow)"; case 0x41: return "KR_TSA";
	case 0x42: return "KR_TSR";       case 0x43: return "KR_LEN";
	case 0x44: return "RASTER_COUNT"; case 0x50: return "KS_MODE(adpcm)";
	case 0x51: return "KS_CR0";       case 0x52: return "KS_CR1";
	case 0x53: return "KS_STAT";      case 0x58: return "KS_START0";
	case 0x59: return "KS_END0";      case 0x5a: return "KS_HALF0";
	case 0x5c: return "KS_START1";    case 0x5d: return "KS_END1";
	case 0x5e: return "KS_HALF1";     case 0x61: return "KRAM_MODE";
	default:   return "?";
	}
}

#define KP_REGW(r)           do { kp_regw[(r) & 0x7F]++; } while(0)
#define KP_REGR(r)           do { kp_regr[(r) & 0x7F]++; } while(0)
#define KP_CPU_WR(pg,ad)     do { kp_hit(0, pg, ad); kx_write(pg, ad); } while(0)
#define KP_CPU_RD(pg,ad)     kp_hit(1, pg, ad)
#define KP_DMA_WR(pg,ad)     kp_hit(2, pg, ad)
#define KP_ADPCM_RD(pg,ad)   do { kp_hit(3, pg, ad); kx_read(pg, ad, 3); } while(0)
#define KP_RAINBOW_RD(pg,ad) do { kp_hit(4, pg, ad); kx_read(pg, ad, 4); } while(0)
#define KP_BG(pg,bat,cg,mode) kp_bg_scanline(pg, bat, cg, mode)

static void kbt_report(unsigned long long frames);
void king_prof_report(unsigned long long frames)
{
 kbt_report(frames);
	double f = frames ? (double)frames : 1.0;
	fprintf(stderr, "\n================  KING (HuC6272) PROFILE  ================\n");
	fprintf(stderr, "KRAM bandwidth / field (16-bit words):\n");
	for(int e = 0; e < KP_NENG; e++)
		fprintf(stderr, "  %-13s %8.0f\n", kp_eng_name[e], kp_eng_cnt[e] / f);
	fprintf(stderr, "  (CPU-write is DOOM's framebuffer stores via out.h -> KRAM_DATA reg 0x0e)\n");

	fprintf(stderr, "\nKING registers touched / field (by name; top 16 by traffic):\n");
	{
		int idx[128], n = 0;
		for(int i = 0; i < 128; i++) if(kp_regw[i] || kp_regr[i]) idx[n++] = i;
		for(int a = 0; a < n; a++) for(int b = a + 1; b < n; b++)
			if(kp_regw[idx[b]] + kp_regr[idx[b]] > kp_regw[idx[a]] + kp_regr[idx[a]]) {
				int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
		int lim = n < 16 ? n : 16;
		for(int a = 0; a < lim; a++) {
			int i = idx[a];
			fprintf(stderr, "  0x%02x %-20s  W=%-8.0f R=%-8.0f\n",
				i, king_reg_name(i), kp_regw[i] / f, kp_regr[i] / f);
		}
	}

	fprintf(stderr, "\nKRAM per-engine footprint (word-address range touched; CPU-write spans the whole\n");
	fprintf(stderr, "page because the framebuffer is page-wide, so use the region map below, not the span):\n");
	for(int pg = 0; pg < 2; pg++)
		for(int e = 0; e < KP_NENG; e++)
			if(kp_lo[e][pg] <= kp_hi[e][pg])
				fprintf(stderr, "  page%d %-13s 0x%05x..0x%05x  (span %u words)\n",
					pg, kp_eng_name[e], kp_lo[e][pg], kp_hi[e][pg],
					(unsigned)(kp_hi[e][pg] - kp_lo[e][pg] + 1));

	/* PITFALL VIEW: engines read KRAM; if the CPU also writes the SAME 1024-word region
	 * (per field, materially), the framebuffer and that engine share memory -> verify it
	 * is an intended double-buffer swap and not a live overwrite (the sky/ADPCM garble bug). */
	fprintf(stderr, "\nKRAM contention (only %d-word regions an engine READS; cpu_wr in the same region\n", 1 << KP_BSHIFT);
	fprintf(stderr, "= framebuffer shares it -> verify intended). '!!' = both busy (>8/field each):\n");
	fprintf(stderr, "  %-4s %-7s %8s %8s %8s %8s %8s\n", "page", "wordad", "cpu_wr", "dma_wr", "adpcm_rd", "rnbow_rd", "bg_rd");
	{
		int shared = 0;
		for(int pg = 0; pg < 2; pg++)
			for(int b = 0; b < KP_NBUCK; b++) {
				double w = kp_buck[0][pg][b] / f, d = kp_buck[2][pg][b] / f,
				       a = kp_buck[3][pg][b] / f, rb = kp_buck[4][pg][b] / f,
				       bg = kp_buck[5][pg][b] / f;
				if(a < 0.5 && rb < 0.5 && d < 0.5 && bg < 0.5) continue;   /* only engine-read regions */
				int clash = (w > 8) && (a > 8 || rb > 8 || d > 8 || bg > 8);
				if(w > 8 && (a > 0.5 || rb > 0.5 || d > 0.5 || bg > 0.5)) shared++;
				fprintf(stderr, "  %-4d 0x%05x %8.0f %8.0f %8.0f %8.0f %8.0f %s\n",
					pg, (unsigned)(b << KP_BSHIFT), w, d, a, rb, bg,
					clash ? " !! shared+busy" : "");
			}
		fprintf(stderr, "%d region(s) written by CPU AND read by an engine — confirm each is a\n", shared);
		fprintf(stderr, "deliberate buffer hand-off, not a same-frame overwrite.\n");
	}

	fprintf(stderr, "\nEXACT same-field collisions (per-word; CPU write vs ADPCM/RAINBOW read of the\n");
	fprintf(stderr, "SAME KRAM word within one field — a genuine read/write hazard, not coarse):\n");
	fprintf(stderr, "  CPU-write-then-engine-read : %llu total (%.2f/field)\n",
		kx_read_then_cpu, kx_read_then_cpu / f);
	fprintf(stderr, "  engine-read-then-CPU-write : %llu total (%.2f/field)\n",
		kx_cpu_then_read, kx_cpu_then_read / f);
	if(kx_nsample) {
		fprintf(stderr, "  first offending words:\n");
		for(int i = 0; i < kx_nsample; i++)
			fprintf(stderr, "    page%d 0x%05x  vs %s\n", kx_sample_pg[i], kx_sample_addr[i],
				kx_sample_eng[i] == 4 ? "RAINBOW" : kx_sample_eng[i] == 3 ? "ADPCM" : "engine");
	} else
		fprintf(stderr, "  NONE — CPU and the audio/video engines never touch the same KRAM word\n"
			"  in the same field. The coarse-region sharing above is safe double-buffering.\n");
	fprintf(stderr, "=========================================================\n");
}
#else
#define KP_REGW(r)
#define KP_REGR(r)
#define KP_CPU_WR(pg,ad)
#define KP_CPU_RD(pg,ad)
#define KP_DMA_WR(pg,ad)
#define KP_ADPCM_RD(pg,ad)
#define KP_RAINBOW_RD(pg,ad)
#define KP_BG(pg,bat,cg,mode)
#endif /* V810_PROFILE */

static uint8 BGLayerDisable;
static bool RAINBOWLayerDisable;

static void RedoKINGIRQCheck(void);

/* C6272 §1.3: MOD=0 constrains all KRAM accesses to page zero.
   Keep the externally programmed page bits readable; mask only physical access. */
static inline unsigned KING_KRAMPage(unsigned requested)
{
 return king->KRAM_Mode ? (requested & 1u) : 0u;
}

static void RecalcKRAMPagePtrs(void)
{
 king->RainbowPagePtr = king->KRAM[KING_KRAMPage((king->PageSetting & 0x1000) ? 1 : 0)];
 king->DMAPagePtr = king->KRAM[KING_KRAMPage(king->PageSetting & 1)];
}

/* Source requests are recorded when a real emulator engine accesses KRAM.
 * The order and phase of physical grants remain partly undocumented. */
static uint32 king_gfx_ts;
static uint32 kbus_event_ts;
static void KING_KBusReserve(unsigned bank, unsigned source, uint32 timestamp);
static uint32 KING_KBusReserveTagged(unsigned bank, unsigned source, uint32 timestamp, unsigned tag);
static void KING_CommitPendingSCSI(uint32 timestamp);
static void KING_CommitPendingADPCM(uint32 timestamp);
static bool KING_QueueSCSIWord(unsigned bank, unsigned address, uint16 value, uint32 timestamp);
static uint64 rainbow_ahead_hits, rainbow_ahead_grants, rainbow_ahead_requests, rainbow_eager_words;
static uint64 scsi_kbus_backpressure_retries;
/* Count reads that reach a reserved HuC6271 prefetch address before its
 * K-BUS grant completed. They still use the synchronous decoder fallback
 * until the JPEG/RLE parser can truly suspend on an input FIFO. */
static uint64 rainbow_read_ahead_underflows;
static void KING_RainbowAheadCommit(uint32 timestamp);
static void KING_RainbowAheadSchedule(uint32 timestamp);
static void KING_RainbowAheadDiscard(void);
static void KING_RainbowAsyncStart(bool first, bool skip, uint32 now);
static void KING_RainbowAsyncCommit(uint32 now);
static void KING_RainbowAsyncCancel(void);



enum { KBUS_SCSI=1, KBUS_RAINBOW=2, KBUS_ADPCM=3, KBUS_REFRESH=4 };

uint8 KING_RB_Fetch(void)
{
 if(king->RBAsyncDecoding)
 {
  /* The decoder can only see fully grant-captured input. Do not call KRAM
   * from Huffman/RLE paths, even for the 0xFF stuffing bytes. */
  if(king->RBAsyncConsumeIndex >= king->RBAsyncLength)
  {
   if(king->RBAsyncUnderflows<12 && getenv("PCFX_RAINBOW_STATS"))
    fprintf(stderr,"RAINBOW async underflow: len=%u consumed=%u target=%u marker=%02x readpos=%05x stage=%u first=%u\n",
      king->RBAsyncLength,king->RBAsyncConsumeIndex,king->RBAsyncTarget,
      king->RBAsyncMarker,king->RAINBOWKRAMReadPos,king->RBAsyncScanState,king->RBAsyncFirst);
   king->RBAsyncUnderflows++;
   /* An unexpected overread is a decoder contract bug: log it.  The
    * completed-block precondition prevents this for valid streams. */
   return 0;
  }
  const uint8 ret=king->RBAsyncStage[king->RBAsyncConsumeIndex++];
  king->RAINBOWKRAMReadPos=((king->RAINBOWKRAMReadPos+1u)&0x3ffffu) |
                             (king->RAINBOWKRAMReadPos&0x40000u);
  return ret;
 }
 const uint32 address=(king->RAINBOWKRAMReadPos>>1)&0x3FFFFu;
 const unsigned page=KING_KRAMPage((king->PageSetting & 0x1000) ? 1u : 0u);
 uint16 word=king->KRAM[page][address];
 const unsigned byte_index=king->RAINBOWKRAMReadPos & 1u;
 bool ahead=false;
 for(unsigned i=0;i<3;i++)
 {
  if(king->RainbowAheadStatus[i]!=2 || king->RainbowAheadPage[i]!=page ||
     king->RainbowAheadAddress[i]!=address) continue;
  word=king->RainbowAheadValue[i];
  ahead=true;
  if(!byte_index) rainbow_ahead_hits++;
  if(king->RAINBOWKRAMReadPos & 1u) king->RainbowAheadStatus[i]=0;
  break;
 }
 if(!ahead && !(king->RAINBOWKRAMReadPos & 1u))
  for(unsigned i=0;i<3;i++)
   if(king->RainbowAheadStatus[i]==1 && king->RainbowAheadPage[i]==page &&
      king->RainbowAheadAddress[i]==address)
   { rainbow_read_ahead_underflows++; break; }
 /* A grant-time prefetch already paid for its K-BUS read. Other words
  * continue to use the v16 eager decoder path, until the decoder is
  * refactored into a resumable, REQR-paced input consumer. */
 if(!ahead && !(king->RAINBOWKRAMReadPos & 1u))
 {
  KP_RAINBOW_RD(page,address);
  rainbow_eager_words++;
  KING_KBusReserve(page,KBUS_RAINBOW,king_gfx_ts);
 }
 king->RAINBOWKRAMReadPos=((king->RAINBOWKRAMReadPos+1)&0x3FFFFu) |
                           (king->RAINBOWKRAMReadPos&0x40000u);
 return (uint8)(word>>(byte_index*8));
}

/* Is the KING real-DMA "trailing item" erratum being modelled?
 *
 * This models real PC-FX hardware: the KING real-DMA-to-KRAM engine retires one
 * REQ/ACK handshake PAST the programmed transfer count, and an exact-length
 * transfer (the request equals the whole sector the drive delivers, so nothing
 * is left in DATA IN to drive that trailing handshake) never retires -- the
 * count register never reaches zero.  A driver that polls the count for
 * completion therefore wedges.  doom-pcfx's boot-time ADPCM bank load is exactly
 * this case, and hangs on real hardware with its progress bar frozen at the
 * first step; see docs/king-dma-erratum.md.
 *
 * It is ON by default so the emulator reproduces that hardware behaviour without
 * any special setup.  Set the environment variable to 0 to disable it (retire
 * on the programmed count, i.e. the pre-hardware-accuracy behaviour) for
 * debugging or for software that was written around the retired-on-count model:
 *
 *   unset / PCFX_KING_DMA_ERRATUM=1   model the hardware quirk (default)
 *   PCFX_KING_DMA_ERRATUM=0           retire on the programmed count
 */
static bool king_dma_erratum_enabled(void)
{
 static int cached = -1;

 if(cached < 0)
 {
#ifdef PCFX_WASM
  /* The browser runtime has no process environment; use the non-debug
   * defaults for these opt-in/opt-out erratum switches. */
  cached = 0;
#else
  const char *e = getenv("PCFX_KING_DMA_ERRATUM");
  /* Default OFF (opt-in via PCFX_KING_DMA_ERRATUM=1).  This models the KING
   * real-DMA-to-KRAM path, which the current doom-pcfx no longer uses -- it
   * loads via CPU PIO now (see king_pio_erratum_enabled()), so the real-DMA
   * erratum no longer reproduces doom's hang and defaulting it on only risks
   * mis-wedging games that legitimately use exact-length real-DMA transfers. */
  cached = (e && *e && *e == '1') ? 1 : 0;
#endif
 }
 return cached ? TRUE : FALSE;
}

/* CPU-PIO SCSI read failure (see the case-0x00 data-register comment and
 * docs/king-pio-read-erratum.md).  Default ON (reproduces doom-pcfx's real-
 * hardware boot hang: its first CD read -- a CPU-PIO ADPCM-bank load -- fails,
 * so pcfx_fatal_blink loops forever).  PCFX_KING_PIO_ERRATUM=0 disables it (the
 * PIO read succeeds, so doom boots to the title -- the pre-accuracy behaviour,
 * and the mode to use once a doom/libpcfx workaround exists). */
static bool king_pio_erratum_enabled(void)
{
 static int cached = -1;

 if(cached < 0)
 {
#ifdef PCFX_WASM
  cached = 1;
#else
  const char *e = getenv("PCFX_KING_PIO_ERRATUM");
  cached = (e && *e && *e == '0') ? 0 : 1;
#endif
  /* One-time notice so it's obvious which build/mode is running. */
  fprintf(stderr, "[KING] PIO-read erratum %s (doom-pcfx boot-hang model; "
                  "PCFX_KING_PIO_ERRATUM=0 to disable)\n",
          cached ? "ENABLED" : "disabled");
 }
 return cached ? TRUE : FALSE;
}

/* The measured real-hardware CPU-PIO CD-read failure is NOT intrinsic to PIO: a
 * homebrew that reads via CPU-PIO with the interval timer OFF (waifu / Shattered
 * Decks -- small metadata PIO reads, bulk via count-0 phase-driven KING DMA)
 * loads fine, while doom-pcfx -- which starts a ~1 ms level-9 interval-timer IRQ
 * (pcfx_time_init, period 1432) BEFORE its first CD read -- fails.  The IRQ fires
 * ~1000x/s inside the tight CPU PIO DATA-IN loop and corrupts the SCSI REQ/ACK
 * handshake, short-cutting the read (probes 027/028/029; the emulator otherwise
 * delivers whole sectors atomically and never sees it).  So a CPU-PIO read fails
 * only when a *fast* periodic timer IRQ is live during it.  Timer control bit0x2
 * = timer enable, bit0x1 = its IRQ enable; EFF_PERIOD = (period?:0x10000)*15 in
 * ~21.48 MHz V810 master cycles.  Ceiling ~4 ms (doom is ~1 ms) so a slow
 * housekeeping timer does not spuriously fail reads. */
static bool king_pio_timer_irq_hot(void)
{
 extern void PCFXTIMER_GetControlPeriod(unsigned *ctrl, unsigned *per);
 unsigned tc = 0, tp = 0;
 unsigned eff_cycles;

 PCFXTIMER_GetControlPeriod(&tc, &tp);
 if((tc & 0x3) != 0x3)          /* timer AND its interrupt must both be enabled */
  return FALSE;
 eff_cycles = (tp ? tp : 0x10000) * 15;
 return (eff_cycles < 86000) ? TRUE : FALSE;   /* < ~4 ms period => fires during the read */
}

/* Called by scsicd.c's RunCDRead() at a SECTOR BOUNDARY -- just before it
 * would queue the next sector of an in-progress DATA_IN read, with the FIFO
 * already drained and REQ deasserted (the driver is between sectors, in its
 * wait-for-REQ/phase-poll loop, not between a data-register byte read and its
 * ACK).  Never checked for the very first sector of a command: that sector
 * always delivers, and it's only the CPU actually reading it (KING_Read16
 * case 0x00, below) that latches PIOReadSeen and reveals the transfer method.
 * A DMA transfer never sets the latch (the real-DMA pump drains the FIFO via
 * its own hardware handshake, not through this CPU register), so DMA reads
 * under a hot timer are correctly let through -- the fix this whole model
 * exists to eventually validate.
 *
 * When TRUE, scsicd.c doesn't just fail the command -- it calls
 * KING_PIOWedgeBus() and drops to BUS FREE: on hardware this first failure
 * poisons the bus until a full SCSI reset (see the PIOBusWedged field
 * comment). */
bool KING_PIOTransferShouldFail(void)
{
 if(!king)
  return FALSE;
 return (king_pio_erratum_enabled() && king->PIOReadSeen &&
         king_pio_timer_irq_hot()) ? TRUE : FALSE;
}

/* The persistent bus-poison latch (see the PIOBusWedged field comment).
 * KING_PIOWedgeBus() is called by scsicd.c at the sector boundary where
 * KING_PIOTransferShouldFail() first fires; KING_PIOBusWedgedNow() is
 * checked by scsicd.c's command dispatch so every subsequent command --
 * the driver's retries, its REQUEST SENSE, its SEEK repositioning -- is
 * silently dropped to BUS FREE, failing as fast as real hardware's do
 * (probe 027: a whole failed 8-attempt retry loop inside one vblank
 * field).  Cleared only by SCSI RST (KING_Write16 RST handling below),
 * KING reset, and power-on -- a full bus reset is the one recovery real
 * hardware responds to. */
void KING_PIOWedgeBus(void)
{
 if(king)
  king->PIOBusWedged = TRUE;
}

bool KING_PIOBusWedgedNow(void)
{
 if(!king)
  return FALSE;
 return king->PIOBusWedged ? TRUE : FALSE;
}

/* Called by scsicd.c when a new READ command's sector count is programmed
 * (DoREADBase), so each attempt's PIO-vs-DMA determination starts fresh --
 * the driver's retry after a failed attempt must be re-observed, not judged
 * by the previous attempt's transfer method.  (The bus wedge above is NOT
 * reset here -- a new command doesn't unpoison the bus; only RST does.) */
void KING_PIOReadResetLatch(void)
{
 if(king)
  king->PIOReadSeen = FALSE;
}

/* While a real-DMA is wedged (it retired one item short and is waiting for a
 * trailing REQ/ACK that never comes -- see king_dma_erratum_enabled()), the KING
 * holds the CPU's I/O bus: every access to a KING register stalls until the DMA
 * can service it, which it never can.  A driver polling the DMA/SCSI registers
 * for completion therefore makes essentially no forward progress -- it is frozen
 * on its poll, not merely reading a stuck value it can spin past and time out on.
 * That is why doom-pcfx's boot bar stays frozen at its first step on real
 * hardware instead of running its read-retry loop out to an error indicator.
 *
 * Charging a large per-access stall models that hold.  The exact hold duration
 * is unobservable and irrelevant; it only has to dwarf any poll loop's own
 * iteration budget so the loop cannot count itself out within any realistic run.
 * Gated on DMARetirePending, which is only ever set with the erratum enabled, so
 * ordinary (retiring) DMAs are completely unaffected. */
#define KING_DMA_WEDGE_STALL_CYCLES 65536 /* 1<<16 */

uint32 KING_DMAWedgeStallCycles(void)
{
 return king->DMARetirePending ? KING_DMA_WEDGE_STALL_CYCLES : 0;
}

/* HuC6272 §3.3.4: SCSI REQ must not be ACKed for a second byte
 * until its completed KRAM halfword can be accepted into the grant queue.
 * false leaves the bus handshake, DMA counter and byte latch unchanged. */
static bool DoRealDMA(uint8 db)
{
 /* The trailing handshake past the programmed count has arrived: it is consumed
  * off the bus and discarded (NOT written to KRAM -- every requested word has
  * landed), and only now does the count fall to zero and the transfer retire.
  * See DMARetirePending's declaration. */
 if(king->DMARetirePending)
 {
  KINGDBG("DMA Done (retired on the trailing item past the programmed count)\n");
  king->DMARetirePending = FALSE;
  king->DMATransferSize = 0;
  king->DMAInterrupt = TRUE;
  RedoKINGIRQCheck();
  king->DMAStatus &= ~1;
  return true;
 }

 if(!king->DMATransferFlipFlop)
 {
  king->DMALatch = db;
  /* The hardware DMA counter counts each received SCSI byte, not only
   * completed KRAM words (HuC6272 §3.3.4 step 7). */
  king->DMATransferSize = (king->DMATransferSize - 1u) & 0x3FFFFu;
 }
 else
 {

  /* HuC6272 §3.3.4: two incoming SCSI bytes become one KRAM word.
   * Capture (bank, address, data) now; only expose it when the bus grant
   * retires. An exhausted queue must not silently drop a DMA word. */
  if(!KING_QueueSCSIWord(KING_KRAMPage(king->PageSetting & 1u),
                         king->DMATransferAddr & 0x3FFFFu,
                         (uint16)(king->DMALatch | (db << 8)), kbus_event_ts))
  {
   scsi_kbus_backpressure_retries++;
   /* Do not consume the second byte or assert ACK: the drive keeps REQ
    * raised until a grant/queue entry becomes available. In particular,
    * no eager KRAM write is ever permitted here. */
   return false;
  }
  KP_DMA_WR(KING_KRAMPage(king->PageSetting & 1), king->DMATransferAddr & 0x3FFFF);
  king->DMATransferAddr = ((king->DMATransferAddr + 1) & 0x1FFFF) | (king->DMATransferAddr & 0x20000);
  king->DMATransferSize = (king->DMATransferSize - 1u) & 0x3FFFFu;
  if(!king->DMATransferSize)
  {
   if(king_dma_erratum_enabled() && king->DMACountBounded)
   {
    /* Every requested word has landed in KRAM, but the engine holds the count
     * one item short of zero until one more handshake occurs.  That last count
     * step is the only thing a driver can observe (reg 0x0A), so hold it at 2
     * and leave DMAStatus bit 0 set, keeping the pump above ACKing:
     *
     *  - leftovers still in DATA IN -> the trailing item arrives at once, the
     *    branch at the top drops the count to 0 and retires.  Data is intact.
     *  - the request exactly consumed what the target had -> it drops to
     *    STATUS, the pump stops (it only runs in DATA IN), the trailing item
     *    never comes, and the count is stuck at 2 forever.  A driver polling
     *    reg 0x0A for zero -- which is what liberis/libpcfx's
     *    eris_scsi_check_dma() ultimately does -- then never sees completion.
     *    That is the hardware wedge being reproduced. */
    KINGDBG("DMA count held one item short; awaiting the trailing item\n");
    king->DMARetirePending = TRUE;
    king->DMATransferSize = 2;
    king->DMATransferFlipFlop ^= 1;
    return true;
   }

   KINGDBG("DMA Done\n");
   CDXFERDBG("DMA retire (count reached 0)\n");
   king->DMAInterrupt = TRUE;
   RedoKINGIRQCheck();
   king->DMAStatus &= ~1;
   return true;
  }
 }
 king->DMATransferFlipFlop ^= 1;
 return true;
}

uint16 FXVCE_Read16(uint32 A)
{
  // bit  4-0: Register number
  // bit 13-5: Raster counter
  // bit 14: In interlace mode and when on odd fields set bit.
  // bit 15: "0" during blanking(h or v), "1" during active screen area
  //	Raster counter increments a few cycles after the start of hblank.

 if(!(A & 0x4))
 {	
	uint16 fullret = 0;

	fullret |= fx_vce.AR;
	fullret |= fx_vce.odd_field ? 0x4000 : 0x0000;
        fullret |= fx_vce.raster_counter << 5;

	if(fx_vce.in_hblank || fx_vce.raster_counter < 22 || fx_vce.raster_counter == 262) 
	 fullret |= 0 << 15; // Clear on blanking
	else
	 fullret |= 1 << 15; // Set on active display.

	return(fullret);
 }
 else
 {
  /* C6261 §2.1.3: the palette data read port is R03; R02 is write-only.
     Other register readback is undocumented. Return the undriven bus sentinel,
     rather than incorrectly executing the R03 auto-increment side effect. */
  if(fx_vce.AR == 0x03)
  {
   uint16 ret = fx_vce.palette_rw_latch;
   fx_vce.palette_rw_offset = (fx_vce.palette_rw_offset + 1) & 0x1FF;
   fx_vce.palette_rw_latch = fx_vce.palette_table[fx_vce.palette_rw_offset];
   return ret;
  }
  return 0xFFFF;  /* Unknown open-bus electrical value, not hardware-guaranteed. */
 }

 return(0);
}

void FXVCE_Write16(uint32 A, uint16 V)
{
 if(!(A & 0x4))
 {
  fx_vce.AR = V & 0x1F;
 }
 else
 {
  switch(fx_vce.AR)
  {
		case 0x00: fx_vce.picture_mode = V;
			   break;

		case 0x01: fx_vce.palette_rw_offset = V & 0x1FF;
			   fx_vce.palette_rw_latch = fx_vce.palette_table[fx_vce.palette_rw_offset];
			   break;

		case 0x02: fx_vce.palette_rw_latch = V;
			   fx_vce.palette_table[fx_vce.palette_rw_offset] = fx_vce.palette_rw_latch;
			   RedoPaletteCache(fx_vce.palette_rw_offset);
			   fx_vce.palette_rw_offset = (fx_vce.palette_rw_offset + 1) & 0x1FF;
			   break;

		case 0x04: fx_vce.palette_offset[0] = V; break;
		case 0x05: fx_vce.palette_offset[1] = V; break;
		case 0x06: fx_vce.palette_offset[2] = V; break;
		case 0x07: fx_vce.palette_offset[3] = V & 0x00FF; break;
		case 0x08: fx_vce.priority[0] = V & 0x0777; break;
		case 0x09: fx_vce.priority[1] = V & 0x7777; break;

		case 0x0a: fx_vce.ChromaKeyY = V; break;
                case 0x0b: fx_vce.ChromaKeyU = V; break;
                case 0x0c: fx_vce.ChromaKeyV = V; break;

		case 0x0d: fx_vce.CCR = V; break;
		case 0x0e: fx_vce.BLE = V; break;
		case 0x0f: fx_vce.SPBL = V; break;

		case 0x10: fx_vce.coefficients[0] = V & 0xFFF; break;
                case 0x11: fx_vce.coefficients[1] = V & 0xFFF; break;

                case 0x12: fx_vce.coefficients[2] = V & 0xFFF; break;
                case 0x13: fx_vce.coefficients[3] = V & 0xFFF; break;

                case 0x14: fx_vce.coefficients[4] = V & 0xFFF; break;
                case 0x15: fx_vce.coefficients[5] = V & 0xFFF; break;
  }
 }
}

static void RedoKINGIRQCheck(void)
{
 bool asserted = 0;

 if(king->ADPCMIRQPending)
 {
  asserted = 1;
 }

 if(king->DMAInterrupt && (king->DMAStatus & 0x2))
 {
  asserted = 1;
 }

 if(king->CDInterrupt)
 {
  asserted = 1;
 }

 if(king->SubChannelInterrupt)
  asserted = 1;

 if(king->RasterIRQPending)
  asserted = 1;

 PCFXIRQ_Assert(PCFXIRQ_SOURCE_KING, asserted);
}

void KING_CDIRQ(int type)
{
 if(type == SCSICD_IRQ_MAGICAL_REQ)
 {
  if(king->Reg02 & 0x2)
  {
   if(SCSICD_GetIO() != ((king->Reg03 >> 0) & 1) ||
      SCSICD_GetCD() != ((king->Reg03 >> 1) & 1) ||
      SCSICD_GetMSG() != ((king->Reg03 >> 2) & 1))
   {
     KINGDBG("Phase mismatch interrupt asserted.\n");
     king->CDInterrupt = TRUE;
     RedoKINGIRQCheck();
   }
  }
 }
}

void KING_StuffSubchannels(uint8 subchannels, int subindex)
{
 if(king->SubChannelControl & 0x1)
 {
  if(subindex == -2)
   king->SubChannelBuf = 0x00;
  else if(subindex == -1)
   king->SubChannelBuf = 0x80;
  else
   king->SubChannelBuf = (subchannels & 0x7F);

  if(king->SubChannelControl & 0x2)
  {
   king->SubChannelInterrupt = TRUE;
   RedoKINGIRQCheck();
  }
 }


}

uint8 KING_Read8(const v810_timestamp_t timestamp, uint32 A)
{
 uint8 ret = KING_Read16(timestamp, A & ~1) >> ((A & 1) * 8);

 //printf("Read8: %04x\n", A);
 return(ret);
}

void KING_EndFrame(v810_timestamp_t timestamp)
{
 PCFX_SetEvent(PCFX_EVENT_KING, KING_Update(timestamp));
 scsicd_ne = SCSICD_Run(timestamp);
 /* CPU/device timestamps restart at zero for the next field.  Keep only the
  * outstanding portion of the posted-write drain across that rebase. */
 if(king->KRAMWriteBusyUntil > timestamp)
  king->KRAMWriteBusyUntil -= timestamp;
 else
  king->KRAMWriteBusyUntil = 0;
 if(king->KRAMWritePending)
  king->KRAMWritePendingAt = king->KRAMWritePendingAt > timestamp ?
                              king->KRAMWritePendingAt - timestamp : 0;
 for(unsigned i=0;i<KING_SCSI_PENDING_MAX;i++)
  if(king->SCSIPendingValid[i])
   king->SCSIPendingGrantAt[i] = king->SCSIPendingGrantAt[i] > (uint32)timestamp ?
                                 king->SCSIPendingGrantAt[i] - (uint32)timestamp : 0;
 for(unsigned ch=0;ch<2;ch++)
  if(king->ADPCMGrantPending[ch])
   king->ADPCMGrantAt[ch] = king->ADPCMGrantAt[ch] > (uint32)timestamp ?
                             king->ADPCMGrantAt[ch] - (uint32)timestamp : 0;
 for(unsigned j=0;j<3;j++)
  if(king->RainbowAheadStatus[j]==1)
   king->RainbowAheadGrantAt[j] = king->RainbowAheadGrantAt[j] > (uint32)timestamp ?
       king->RainbowAheadGrantAt[j] - (uint32)timestamp : 0;
 if(king->RBAsyncPending)
  king->RBAsyncGrantAt=king->RBAsyncGrantAt > (uint32)timestamp ?
     king->RBAsyncGrantAt-(uint32)timestamp : 0;
 /* Relative time rebasing may not be a multiple of four, so rehash
  * retained future reservations instead of changing just their tags. */
 for(unsigned b=0;b<2;b++)
 {
  uint32 times[KING_KBUS_SLOTS] = {0};
  uint8 sources[KING_KBUS_SLOTS] = {0};
  uint16 tags[KING_KBUS_SLOTS] = {0};
  for(unsigned i=0;i<KING_KBUS_SLOTS;i++)
   if(king->KBusSlotSource[b][i] && king->KBusSlotTime[b][i] >= (uint32)timestamp)
   {
    const uint32 ts = king->KBusSlotTime[b][i] - (uint32)timestamp;
    const unsigned slot = (ts/4u) & (KING_KBUS_SLOTS-1u);
    times[slot] = ts;
    sources[slot] = king->KBusSlotSource[b][i];
    tags[slot] = king->KBusSlotTag[b][i];
   }
  memcpy(king->KBusSlotTime[b],times,sizeof(times));
  memcpy(king->KBusSlotSource[b],sources,sizeof(sources));
  memcpy(king->KBusSlotTag[b],tags,sizeof(tags));
 }


}

void KING_ResetTS(v810_timestamp_t ts_base)
{
 SCSICD_ResetTS(ts_base);

 king->lastts = ts_base;

 if(king->dma_cycle_counter & 0x40000000)
 {
  king->dma_cycle_counter = 0x7FFFFFFF;
 }
}

//static inline void StartKingMagic(void)
//{
// king->lastts = v810_timestamp;
// king->dma_cycle_counter = KING_MAGIC_INTERVAL;
// PCFX_SetEvent(PCFX_EVENT_KING, KING_MAGIC_INTERVAL);
//}

static inline int32 CalcNextEvent(int32 next_event)
{
 if(king->dma_cycle_counter < next_event)
  next_event = king->dma_cycle_counter;

 if(scsicd_ne < next_event)
  next_event = scsicd_ne;

 return(next_event);
}

static int32 CalcNextExternalEvent(int32 next_event)
{
 // 100 = Hack to make the emulator go faster during CD DMA transfers.
 if(king->dma_cycle_counter < next_event)
  next_event = 100;	//king->dma_cycle_counter;

 if(scsicd_ne < next_event)
  next_event = scsicd_ne;

 if(next_event > HPhaseCounter)
  next_event = HPhaseCounter;

 //printf("KING: %d %d %d; %d\n", king->dma_cycle_counter, scsicd_ne, HPhaseCounter, next_event);

 for(uint_fast8_t chip = 0; chip < 2; chip++)
 {
  int fwoom = (fx_vce.vdc_event[chip] * fx_vce.dot_clock_ratio - fx_vce.clock_divider);

  if(fwoom < 1)
   fwoom = 1;

  if(next_event > fwoom)
   next_event = fwoom;
 }

 return(next_event);
}

/* Post-drain KRAM visibility, enabled by the default slot model.  The CPU-side write address advances at acceptance;
 * the captured (page,address,value) is committed at the deadline. */
static void KING_CommitPendingKRAM(v810_timestamp_t now)
{
 if(king->KRAMWritePending && king->KRAMWritePendingAt <= now)
 {
  king->KRAM[king->KRAMWritePendingPage & 1u]
            [king->KRAMWritePendingAddress & 0x3FFFFu] = king->KRAMWritePendingValue;
  king->KRAMWritePending = 0;
 }
}

/* HuC6272 §3.2.2 specifies the priority relations, not electrical
 * arbitration phase or a 4-clock service duration. Preserve the existing
 * KING-dot-sized grant model as an explicitly uncalibrated quantum.
 * Internal source values are 1=SCSI, 2=HuC6271, 3=ADPCM, 4=REF. */
static unsigned KING_KBusPriority(unsigned source, bool hblank)
{
 if(hblank)
 {
  if(source == KBUS_REFRESH) return 4;
  if(source == KBUS_ADPCM) return 3;
  if(source == KBUS_RAINBOW) return 2;
  if(source == KBUS_SCSI) return 1;
 }
 else
 {
  if(source == KBUS_RAINBOW) return 2;
  if(source == KBUS_SCSI) return 1;
  /* No REF or ADPCM requests are specified for active video. */
 }
 return 0;
}

static bool KING_KBusBGAt(unsigned bank, uint32 when, uint32 origin)
{
 if(HPhase != HPHASE_ACTIVE || fx_vce.in_hblank || !(king->MPROGControl & 1u)) return false;
 const uint32 remaining = HPhaseCounter > 0 ? (uint32)HPhaseCounter : 0;
 if(when < origin || when - origin >= remaining) return false;
 const uint32 elapsed = HPhaseCounter <= 1024 ? 1024u - (uint32)HPhaseCounter : 0;
 const unsigned slot = ((elapsed + when - origin) / 4u) & 7u;
 const uint16 mp = king->MPROGData[(bank & 1u) * 8u + slot];
 return !(mp & 0x100u) && ((king->priority >> (((mp >> 6) & 3u) * 3u)) & 7u);
}

/* v16: grant reservations carry an optional SCSI-word token. If a later
 * higher-priority requester displaces the word, update its completion time;
 * otherwise the earlier deadline would commit data before the bus grant. */
static uint32 KING_KBusReserveTagged(unsigned bank, unsigned source, uint32 now, unsigned tag)
{
 bank = KING_KRAMPage(bank);
 const bool blank = HPhase != HPHASE_ACTIVE || fx_vce.in_hblank;
 if(!KING_KBusPriority(source, blank)) return now;
 uint32 candidate = (now + 3u) & ~3u;
 for(unsigned tries=0;tries<KING_KBUS_SLOTS;tries++,candidate+=4u)
 {
  if(KING_KBusBGAt(bank,candidate,now)) continue;
  const unsigned i = (candidate/4u) & (KING_KBUS_SLOTS-1u);
  const unsigned previous = king->KBusSlotSource[bank][i];
  if(previous && king->KBusSlotTime[bank][i] == candidate)
  {
   if(KING_KBusPriority(source,blank) <= KING_KBusPriority(previous,blank)) continue;
   /* Higher priority wins; carry lower-priority request and its token. */
   const unsigned previous_tag=king->KBusSlotTag[bank][i];
   king->KBusSlotSource[bank][i]=(uint8)source;
   king->KBusSlotTag[bank][i]=(uint16)tag;
   if(tag && tag <= KING_SCSI_PENDING_MAX)
    king->SCSIPendingGrantAt[tag-1]=candidate+4u;
   else if(tag > KING_SCSI_PENDING_MAX && tag <= KING_SCSI_PENDING_MAX+2u)
    king->ADPCMGrantAt[tag-KING_SCSI_PENDING_MAX-1u]=candidate+4u;
   else if(tag > KING_SCSI_PENDING_MAX+2u && tag <= KING_SCSI_PENDING_MAX+5u)
    king->RainbowAheadGrantAt[tag-KING_SCSI_PENDING_MAX-3u]=candidate+4u;
   else if(tag==KING_SCSI_PENDING_MAX+6u)
    king->RBAsyncGrantAt=candidate+4u;
   source=previous;
   tag=previous_tag;
   continue;
  }
  king->KBusSlotTime[bank][i]=candidate;
  king->KBusSlotSource[bank][i]=(uint8)source;
  king->KBusSlotTag[bank][i]=(uint16)tag;
  if(tag && tag <= KING_SCSI_PENDING_MAX)
   king->SCSIPendingGrantAt[tag-1]=candidate+4u;
  else if(tag > KING_SCSI_PENDING_MAX && tag <= KING_SCSI_PENDING_MAX+2u)
   king->ADPCMGrantAt[tag-KING_SCSI_PENDING_MAX-1u]=candidate+4u;
  else if(tag > KING_SCSI_PENDING_MAX+2u && tag <= KING_SCSI_PENDING_MAX+5u)
   king->RainbowAheadGrantAt[tag-KING_SCSI_PENDING_MAX-3u]=candidate+4u;
  else if(tag==KING_SCSI_PENDING_MAX+6u)
   king->RBAsyncGrantAt=candidate+4u;
  return candidate+4u;
 }
 /* The manuals do not define a queue overflow policy; test and report it. */
 return UINT32_MAX;
}

static void KING_KBusReserve(unsigned bank, unsigned source, uint32 timestamp)
{
 (void)KING_KBusReserveTagged(bank, source, timestamp, 0);
}

static bool KING_QueueSCSIWord(unsigned bank, unsigned address, uint16 value, uint32 timestamp)
{
 for(unsigned n=0;n<KING_SCSI_PENDING_MAX;n++)
 {
  if(king->SCSIPendingValid[n]) continue;
  king->SCSIPendingValid[n]=1;
  king->SCSIPendingCount++;
  king->SCSIPendingAddress[n]=address & 0x3FFFFu;
  king->SCSIPendingPage[n]=(uint8)KING_KRAMPage(bank);
  king->SCSIPendingValue[n]=value;
  const uint32 deadline=KING_KBusReserveTagged(bank,KBUS_SCSI,timestamp,n+1);
  if(deadline==UINT32_MAX)
  {
   king->SCSIPendingValid[n]=0;
   king->SCSIPendingCount--;
   return false;
  }
  /* Deadline may have been moved during priority arbitration. */
  return true;
 }
 return false;
}

static void KING_CommitPendingSCSI(uint32 timestamp)
{
 if(!king->SCSIPendingCount) return;
 for(unsigned n=0;n<KING_SCSI_PENDING_MAX;n++)
 {
  if(!king->SCSIPendingValid[n] || king->SCSIPendingGrantAt[n]>timestamp) continue;
  const unsigned bank=king->SCSIPendingPage[n]&1u;
  king->KRAM[bank][king->SCSIPendingAddress[n]&0x3FFFFu]=king->SCSIPendingValue[n];
  king->SCSIPendingValid[n]=0;
  king->SCSIPendingCount--;
  const uint32 slot_time=king->SCSIPendingGrantAt[n]-4u;
  const unsigned slot=(slot_time/4u)&(KING_KBUS_SLOTS-1u);
  if(king->KBusSlotTag[bank][slot]==n+1 &&
     king->KBusSlotTime[bank][slot]==slot_time)
   king->KBusSlotTag[bank][slot]=0;
 }
}

static void KING_CommitPendingADPCM(uint32 timestamp)
{
 for(unsigned ch=0;ch<2;ch++)
 {
  if(!king->ADPCMGrantPending[ch] || king->ADPCMGrantAt[ch]>timestamp) continue;
  const unsigned page=king->ADPCMGrantPage[ch]&1u;
  const uint32 address=king->ADPCMGrantAddress[ch]&0x3FFFFu;
  const uint16 value=king->KRAM[page][address];
  KP_ADPCM_RD(page,address);
  king->ADPCMGrantPending[ch]=0;
  /* A final word must still arrive even if its address hit the end and
   * R50 auto-disabled the channel on this very request. An explicit CPU
   * disable, by contrast, cancels any pending word below. */
  SoundBox_ADPCMReceiveWord(ch,value);
  const uint32 grant=king->ADPCMGrantAt[ch]-4u;
  const unsigned slot=(grant/4u)&(KING_KBUS_SLOTS-1u);
  if(king->KBusSlotTag[page][slot]==KING_SCSI_PENDING_MAX+ch+1u &&
     king->KBusSlotTime[page][slot]==grant)
   king->KBusSlotTag[page][slot]=0;
 }
}

/* Stage exactly three post-block words, as required by HuC6272's
 * documented 0x0000 guard.  Capture bytes at K-BUS grant, not request.
 * No assumption is made about the HuC6271's internal FIFO depth beyond
 * these three documented over-read words. */
static void KING_RainbowAheadDiscard(void)
{
 for(unsigned j=0;j<3;j++)
 {
  if(king->RainbowAheadStatus[j]==1)
  {
   const unsigned bank=king->RainbowAheadPage[j]&1u;
   const uint32 grant=king->RainbowAheadGrantAt[j]-4u;
   const unsigned slot=(grant/4u)&(KING_KBUS_SLOTS-1u);
   if(king->KBusSlotTime[bank][slot]==grant &&
      king->KBusSlotTag[bank][slot]==KING_SCSI_PENDING_MAX+3u+j)
   {
    king->KBusSlotTag[bank][slot]=0;
    king->KBusSlotSource[bank][slot]=0;
   }
  }
  king->RainbowAheadStatus[j]=0;
 }
}

static void KING_RainbowAheadSchedule(uint32 timestamp)
{
 KING_RainbowAheadDiscard();
 const unsigned bank=KING_KRAMPage((king->PageSetting & 0x1000u)?1u:0u);
 const uint32 addr=(king->RAINBOWKRAMReadPos>>1)&0x3FFFFu;
 for(unsigned j=0;j<3;j++)
 {
  king->RainbowAheadAddress[j]=(addr+j)&0x3FFFFu;
  king->RainbowAheadPage[j]=bank;
  king->RainbowAheadStatus[j]=1;
  rainbow_ahead_requests++;
  const uint32 grant=KING_KBusReserveTagged(bank,KBUS_RAINBOW,timestamp,
                                           KING_SCSI_PENDING_MAX+3u+j);
  if(grant==UINT32_MAX) king->RainbowAheadStatus[j]=0;
 }
}

static void KING_RainbowAheadCommit(uint32 timestamp)
{
 for(unsigned j=0;j<3;j++)
 {
  if(king->RainbowAheadStatus[j]!=1 || king->RainbowAheadGrantAt[j]>timestamp) continue;
  const unsigned bank=king->RainbowAheadPage[j]&1u;
  const uint32 grant=king->RainbowAheadGrantAt[j]-4u;
  king->RainbowAheadValue[j]=king->KRAM[bank][king->RainbowAheadAddress[j]&0x3FFFFu];
  king->RainbowAheadStatus[j]=2;
  rainbow_ahead_grants++;
  const unsigned slot=(grant/4u)&(KING_KBUS_SLOTS-1u);
  if(king->KBusSlotTime[bank][slot]==grant &&
     king->KBusSlotTag[bank][slot]==KING_SCSI_PENDING_MAX+3u+j)
   king->KBusSlotTag[bank][slot]=0;
 }
}

/* HuC6272 §3.4: RAINBOW requests words through the K-BUS. RLE tokens are
 * decoded incrementally as their complete bytes are captured. JPEG Huffman
 * decoding remains block-atomic pending a resumable bit-reservoir backend. */
/* Incremental JPEG F8/FF input parser. A trailing FF is a two-byte
 * operation, so its successor must arrive at a K-BUS grant before either
 * byte is committed. The 128 quantization bytes of FF are separate and
 * are not subject to entropy byte stuffing. This is deliberately not a
 * substitute for the still-block-atomic Huffman/IDCT backend. */
/* Optional diagnostic: default rendering avoids a second IDCT workload. */
static int KING_RainbowMCUCompareEnabled(void)
{
 static int value=-1;
 if(value<0)
 {
  const char *p=getenv("PCFX_RAINBOW_MCU_COMPARE");
  value=(p && *p && strcmp(p,"0"))?1:0;
 }
 return value;
}

static void KING_RainbowMCUColumn(void *opaque,const RBH_State *state,unsigned column)
{
 if(!KING_RainbowMCUCompareEnabled() || !king->RBMCUQuantValid || column>=16) return;
 uint8 luma[256];
 RBM_ReconstructLuma(state,king->RBMCUQuantBase,luma,RAINBOW_IsFastBackend());
 for(unsigned y=0;y<16;y++)
  memcpy(&king->RBMCULuma[y*256+column*16],luma+y*16,16);
 king->RBMCUValidColumn[column]=1;
 king->RBMCUColumns++;
 (void)opaque;
}

static void KING_RainbowJPEGProgress(void)
{
 if(!king->RBAsyncActive || !king->RBAsyncTarget ||
    (king->RBAsyncMarker!=0xf8 && king->RBAsyncMarker!=0xff)) return;
 if(!king->RBJPEGActive)
 {
  king->RBJPEGActive=1;
  king->RBJPEGCursor=king->RBAsyncScanIndex;
  king->RBJPEGEntropyEnd=king->RBAsyncTarget-8u;
  king->RBJPEGQuantExpected=(king->RBAsyncMarker==0xff)?128:0;
  king->RBJPEGQuantCount=0;
  king->RBJPEGDigest=2166136261u;
  memset(king->RBMCUValidColumn,0,sizeof(king->RBMCUValidColumn));
 }
 const uint32 end=king->RBJPEGEntropyEnd;
 while(king->RBJPEGCursor<end && king->RBJPEGCursor<king->RBAsyncLength)
 {
  uint32 at=king->RBJPEGCursor;
  uint8 b=king->RBAsyncStage[at];
  if(king->RBJPEGQuantCount<king->RBJPEGQuantExpected)
  {
   king->RBJPEGQuant[king->RBJPEGQuantCount++]=b;
   king->RBJPEGCursor++;
   continue;
  }
  /* The decoder drops the stuffed byte following a literal FF. Preserve
   * the original FF and its companion atomically across grant boundaries. */
  if(b==0xff && at+1u<end && at+1u>=king->RBAsyncLength)
  {
   king->RBJPEGStalledPairs++;
   break;
  }
  king->RBJPEGCursor++;
  if(b==0xff && king->RBJPEGCursor<end)
  {
   king->RBJPEGCursor++;
   king->RBJPEGStuffedPairs++;
  }
  king->RBJPEGDigest=(king->RBJPEGDigest^b)*16777619u;
  king->RBJPEGEntropyBytes++;
 }
 if(king->RBJPEGQuantCount==king->RBJPEGQuantExpected)
 {
  if(king->RBJPEGQuantExpected==128 && !king->RBHStarted)
  {
   memcpy(king->RBMCUQuantBase,king->RBJPEGQuant,64);
   king->RBMCUQuantValid=1;
  }
  if(!king->RBHStarted)
  {
   RBH_Start(&king->RBH, king->RBAsyncScanIndex+king->RBJPEGQuantExpected,
             king->RBJPEGEntropyEnd);
   king->RBHStarted=1;
   /* An FF block loads fresh quant tables, so discard the previous
    * block's effective rescale; F8 inherits it. */
   if(king->RBJPEGQuantExpected==128) king->RBMCUScale=4;
   king->RBH.quant_scale=king->RBMCUScale;
  }
  RBH_AdvanceColumns(&king->RBH,king->RBAsyncStage,king->RBAsyncLength,
                     4096,KING_RainbowMCUColumn,NULL);
  RBH_DrainTrailingRescales(&king->RBH,king->RBAsyncStage,king->RBAsyncLength);
  king->RBMCUScale=king->RBH.quant_scale;
 }
}

static void KING_RainbowAsyncCancel(void)
{
 if(king->RBAsyncPending)
 {
  const unsigned bank=king->RBAsyncPage&1u;
  const uint32 grant=king->RBAsyncGrantAt>=4?king->RBAsyncGrantAt-4:0;
  const unsigned slot=(grant/4u)&(KING_KBUS_SLOTS-1u);
  if(king->KBusSlotTag[bank][slot]==KING_SCSI_PENDING_MAX+6u &&
     king->KBusSlotTime[bank][slot]==grant)
  {
   king->KBusSlotTag[bank][slot]=0;
   king->KBusSlotSource[bank][slot]=0;
  }
 }
 king->RBAsyncActive=king->RBAsyncPending=king->RBAsyncDecoding=0;
 king->RBAsyncRLEActive=0;
 king->RBJPEGActive=0;
 king->RBHStarted=0;
 king->RBAsyncLength=king->RBAsyncConsumeIndex=0;
 king->RBAsyncScanIndex=king->RBAsyncTarget=0;
 king->RBAsyncProducerPos=king->RAINBOWKRAMReadPos;
}

/* Scan the same 0xff marker, type and 16-bit length as DecodeBlock(),
 * without interpreting Huffman coefficients or prematurely sampling KRAM.
 * Includes FF stuffing/quant tables as bytes of the declared payload. */
static void KING_RainbowAsyncScan(void)
{
 while(king->RBAsyncScanIndex<king->RBAsyncLength && !king->RBAsyncTarget)
 {
  const uint8 b=king->RBAsyncStage[king->RBAsyncScanIndex++];
  switch(king->RBAsyncScanState)
  {
   case 0:
    if(b==0xff) king->RBAsyncScanState=1;
    else if(king->RBAsyncHeaderRemaining) king->RBAsyncHeaderRemaining--;
    break;
   case 1:
    king->RBAsyncMarker=b;
    if(b==0xf0 || b==0xf1 || b==0xf2 || b==0xf3 || b==0xf8 || b==0xff)
     king->RBAsyncScanState=2;
    else { king->RBAsyncScanState=0; if(king->RBAsyncHeaderRemaining) king->RBAsyncHeaderRemaining--; }
    break;
   case 2: king->RBAsyncSizeHigh=b; king->RBAsyncScanState=3; break;
   case 3:
   {
    const int32 length=(int32)((king->RBAsyncSizeHigh<<8)|b)-2;
    if(length>0)
    {
     /* The declared block length includes the two inner dummy bytes,
      * and HuC6272 §3.4.2(4) requires three additional zero guard
      * words after the block. Grant all eight bytes before decoding. */
     king->RBAsyncTarget=king->RBAsyncScanIndex+(uint32)length+8u;
     king->RBAsyncScanState=5;
    }
    else if(king->RBAsyncMarker==0xff)
    {
     king->RBAsyncScanState=4;
     king->RBAsyncSkipRemaining=128;
    }
    else king->RBAsyncScanState=0;
    break;
   }
   case 4:
    if(--king->RBAsyncSkipRemaining==0) king->RBAsyncScanState=0;
    break;
   default: break;
  }
  if(!king->RBAsyncTarget && king->RBAsyncScanIndex>=512u+256u)
  {
   /* A corrupt block cannot monopolize the producer forever. */
   king->RBAsyncTarget=king->RBAsyncScanIndex;
  }
 }
}

static void KING_RainbowAsyncRequest(uint32 now)
{
 if(!king->RBAsyncActive || king->RBAsyncPending ||
    (king->RBAsyncTarget && king->RBAsyncLength>=king->RBAsyncTarget)) return;
 if(king->RBAsyncLength+2>=sizeof(king->RBAsyncStage)) return;
 const unsigned bank=KING_KRAMPage((king->PageSetting&0x1000u)?1u:0u);
 king->RBAsyncAddress=(king->RBAsyncProducerPos>>1)&0x3ffffu;
 king->RBAsyncPage=(uint8)bank;
 const uint32 deadline=KING_KBusReserveTagged(bank,KBUS_RAINBOW,now,
                                             KING_SCSI_PENDING_MAX+6u);
 if(deadline==UINT32_MAX) { king->RBAsyncWaits++; return; }
 king->RBAsyncPending=1;
 king->RBAsyncGrantAt=deadline;
}

/* A complete RLE token is an atomic HuC6271 decoder operation.  A run may
 * cross one or several delayed K-BUS grants; do not consume its first byte
 * until all of its stuffed/count bytes are present. */
static void KING_RainbowAsyncProgressRLE(void)
{
 if(!king->RBAsyncActive || !king->RBAsyncTarget ||
    king->RBAsyncMarker<0xf0 || king->RBAsyncMarker>0xf3) return;
 if(!king->RBAsyncRLEActive)
 {
  king->RBAsyncRLEActive=1;
  king->RBAsyncRLECursor=king->RBAsyncScanIndex;
  king->RBAsyncRLEEnd=king->RBAsyncTarget-8u;
  king->RBAsyncRLEX=0;
  king->RBAsyncRLETokenCount=0;
  RAINBOW_RLEBegin(king->RBAsyncFirst!=0);
  /* The header scanner never consumes decoder bytes.  Commit its position
   * separately when switching to the incremental decoder. */
  const uint32 consumed=king->RBAsyncRLECursor;
  king->RAINBOWKRAMReadPos=((king->RAINBOWKRAMReadPos+consumed)&0x3ffffu) |
                              (king->RAINBOWKRAMReadPos&0x40000u);
 }
 const unsigned shift=4u-(king->RBAsyncMarker&3u);
 const unsigned mask=(1u<<shift)-1u;
 while(king->RBAsyncRLECursor<king->RBAsyncRLEEnd)
 {
  const uint32 cursor=king->RBAsyncRLECursor;
  if(cursor>=king->RBAsyncLength) break;
  const uint8 boot=king->RBAsyncStage[cursor];
  uint32 need=1, count;
  if(boot==0xff) ++need; /* byte stuffing */
  if(!(boot&mask))
  {
   if(cursor+need>=king->RBAsyncLength) break;
   const uint8 count_byte=king->RBAsyncStage[cursor+need];
   ++need;
   if(count_byte==0xff) ++need;
   count=(unsigned)count_byte+1u;
  }
  else count=boot&mask;
  if(cursor+need>king->RBAsyncLength) break;
  /* Declared lengths include the two dummy bytes but exclude the external
   * guard words.  Do not allow a malformed RLE token to consume either. */
  if(cursor+need>king->RBAsyncRLEEnd+2u) break;
  RAINBOW_RLEEmit(king->RBAsyncRLEX,count,boot>>shift);
  king->RBAsyncRLEX+=count;
  king->RBAsyncRLECursor+=need;
  king->RBAsyncRLETokenCount++;
  king->RBAsyncRLETotalTokens++;
  king->RAINBOWKRAMReadPos=((king->RAINBOWKRAMReadPos+need)&0x3ffffu) |
                               (king->RAINBOWKRAMReadPos&0x40000u);
 }
}

static void KING_RainbowAsyncFinish(uint32 now)
{
 if(!king->RBAsyncActive || !king->RBAsyncTarget) return;
 KING_RainbowAsyncProgressRLE();
 if(king->RBAsyncLength<king->RBAsyncTarget) return;
 if(king->RBAsyncRLEActive)
 {
  if(king->RBAsyncRLECursor<king->RBAsyncRLEEnd)
  {
   /* A truncated token is invalid even after the two inner dummy bytes;
    * never fabricate extra input that has not arrived through K-BUS. */
   king->RBAsyncUnderflows++;
   KING_RainbowAsyncCancel();
   return;
  }
  RAINBOW_RLEEnd();
  king->RBAsyncRLEBlocks++;
  king->RBAsyncConsumeIndex=king->RBAsyncRLECursor;
  king->RBAsyncRLEActive=0;
 }
 else
 {
  king->RBAsyncDecoding=1;
  king->RBAsyncConsumeIndex=0;
  RAINBOW_DecodeBlock(king->RBAsyncFirst!=0,king->RBAsyncSkip!=0);
  if(KING_RainbowMCUCompareEnabled() && !king->RBAsyncSkip && king->RBHStarted && king->RBH.finished &&
     king->RBMCUQuantValid && king->RBAsyncMarker>=0xf8)
  {
   uint32 compared=0,diff=0;
   RAINBOW_CompareLuma(king->RBMCULuma,king->RBMCUValidColumn,&compared,&diff);
   king->RBMCUCompared+=compared;
   king->RBMCUDifferences+=diff;
   king->RBMCUBlocks++;
   if(!diff) king->RBMCUExactBlocks++;
  }
  king->RBAsyncDecoding=0;
  if(king->RBJPEGActive) king->RBJPEGBlocks++;
  if(king->RBHStarted && king->RBH.finished) king->RBHCompletedBlocks++;
 }
 const uint32 used=king->RBAsyncConsumeIndex;
 if(used>king->RBAsyncLength) { king->RBAsyncUnderflows++; KING_RainbowAsyncCancel(); return; }
 memmove(king->RBAsyncStage,king->RBAsyncStage+used,king->RBAsyncLength-used);
 king->RBAsyncLength-=used;
 /* Producer remains ahead of the consumer if the parser used fewer bytes. */
 king->RBAsyncActive=0;
 king->RBAsyncRLEActive=0;
 king->RBJPEGActive=0;
 king->RBHStarted=0;
 king->RBAsyncScanIndex=king->RBAsyncTarget=0;
 /* The producer already captured the two dummy bytes and three guard
  * words at K-BUS grant. Never double-reserve them. */
}

static void KING_RainbowAsyncStart(bool first, bool skip, uint32 now)
{
 if(king->RBAsyncActive) return;
 king->RBAsyncActive=1;
 king->RBAsyncFirst=(uint8)first;
 king->RBAsyncSkip=(uint8)skip;
 king->RBAsyncRLEActive=0;
 king->RBJPEGActive=0;
 king->RBHStarted=0;
 king->RBAsyncScanIndex=king->RBAsyncTarget=0;
 king->RBAsyncScanState=0;
 king->RBAsyncHeaderRemaining=512;
 king->RBAsyncSkipRemaining=0;
 KING_RainbowAsyncScan();
 KING_RainbowJPEGProgress();
 KING_RainbowAsyncFinish(now);
 KING_RainbowAsyncRequest(now);
}

static void KING_RainbowAsyncCommit(uint32 now)
{
 if(king->RBAsyncPending && king->RBAsyncGrantAt<=now)
 {
  const unsigned bank=king->RBAsyncPage&1u;
  const uint32 grant=king->RBAsyncGrantAt-4u;
  const unsigned slot=(grant/4u)&(KING_KBUS_SLOTS-1u);
  /* Sampling at the grant sees the KRAM contents *after* older writes
   * committed, not the contents at the time of the request. */
  const uint16 word=king->KRAM[bank][king->RBAsyncAddress&0x3ffffu];
  const unsigned odd=king->RBAsyncProducerPos&1u;
  if(king->RBAsyncLength+2u<=sizeof(king->RBAsyncStage))
  {
   if(!odd) king->RBAsyncStage[king->RBAsyncLength++]=(uint8)word;
   king->RBAsyncStage[king->RBAsyncLength++]=(uint8)(word>>8);
   king->RBAsyncProducerPos=((king->RBAsyncProducerPos+(odd?1u:2u))&0x3ffffu) |
                              (king->RBAsyncProducerPos&0x40000u);
   king->RBAsyncWords++;
  }
  if(king->KBusSlotTime[bank][slot]==grant &&
     king->KBusSlotTag[bank][slot]==KING_SCSI_PENDING_MAX+6u)
  {
   king->KBusSlotTag[bank][slot]=0;
   king->KBusSlotSource[bank][slot]=0;
  }
  king->RBAsyncPending=0;
  KING_RainbowAsyncScan();
  KING_RainbowJPEGProgress();
  KING_RainbowAsyncFinish(now);
 }
 KING_RainbowAsyncRequest(now);
}

static void MDFN_FASTCALL KING_RunGfx(int32 clocks);


v810_timestamp_t MDFN_FASTCALL KING_Update(const v810_timestamp_t timestamp)
{
 int32 clocks = timestamp - king->lastts;
 uint32 running_timestamp = king->lastts;

 //printf("KING Run for: %d\n", clocks);

 king->lastts = timestamp;

 king_gfx_ts = running_timestamp;
 /* Do not expose accepted CPU or SCSI words to BG/RAINBOW/ADPCM before the
  * reserved bus-grant completion. Split long graphics callbacks at all
  * pending completions, preserving the old exact 1024-clock video path. */
 while(running_timestamp < (uint32)timestamp)
 {
  KING_CommitPendingKRAM(running_timestamp);
  KING_CommitPendingSCSI(running_timestamp);
  KING_CommitPendingADPCM(running_timestamp);
  KING_RainbowAheadCommit(running_timestamp);
  KING_RainbowAsyncCommit(running_timestamp);
  uint32 next=(uint32)timestamp;
  if(king->KRAMWritePending && (uint32)king->KRAMWritePendingAt>running_timestamp &&
     (uint32)king->KRAMWritePendingAt<next) next=(uint32)king->KRAMWritePendingAt;
  for(unsigned n=0;king->SCSIPendingCount && n<KING_SCSI_PENDING_MAX;n++)
   if(king->SCSIPendingValid[n] && king->SCSIPendingGrantAt[n]>running_timestamp &&
      king->SCSIPendingGrantAt[n]<next) next=king->SCSIPendingGrantAt[n];
  for(unsigned ch=0;ch<2;ch++)
   if(king->ADPCMGrantPending[ch] && king->ADPCMGrantAt[ch]>running_timestamp &&
      king->ADPCMGrantAt[ch]<next) next=king->ADPCMGrantAt[ch];
  for(unsigned j=0;j<3;j++)
   if(king->RainbowAheadStatus[j]==1 &&
      king->RainbowAheadGrantAt[j]>running_timestamp &&
      king->RainbowAheadGrantAt[j]<next) next=king->RainbowAheadGrantAt[j];
  if(king->RBAsyncPending && king->RBAsyncGrantAt>running_timestamp &&
     king->RBAsyncGrantAt<next) next=king->RBAsyncGrantAt;
  KING_RunGfx((int32)(next-running_timestamp));
  running_timestamp=next;
 }
 KING_CommitPendingKRAM(timestamp);
 KING_CommitPendingSCSI(timestamp);
 KING_CommitPendingADPCM(timestamp);
 KING_RainbowAheadCommit(timestamp);
 KING_RainbowAsyncCommit(timestamp);
 /* Device timing / SCSI pump traverses the same time interval below. */
 running_timestamp=king->lastts-(uint32)clocks;

 while(clocks > 0)
 {
  int32 chunk_clocks = CalcNextEvent(clocks);

  running_timestamp += chunk_clocks;
  clocks -= chunk_clocks;

  scsicd_ne -= chunk_clocks;
  if(scsicd_ne <= 0)
   scsicd_ne = SCSICD_Run(running_timestamp);

  king->dma_cycle_counter -= chunk_clocks;
  if(king->dma_cycle_counter <= 0)
  {
   //assert(king->dma_receive_active || king->dma_send_active);
   king->dma_cycle_counter += KING_MAGIC_INTERVAL;
   if(king->dma_receive_active)
   {
    if(!SCSICD_GetCD() && SCSICD_GetIO())
    {    
     if(SCSICD_GetREQ() && !SCSICD_GetACK())
     {
      /* The trailing item past an exact-count transfer has appeared: the DMA
       * retires now.  Crucially it is NOT consumed here -- it is left in DATA
       * IN so the driver drains the leftover sector itself (real HW "receives
       * a full sector but disposes the leftovers" via the driver, it does not
       * silently eat one byte into the retired DMA).  Consuming it corrupts a
       * following read that reuses the leftover (e.g. Team Innocent). */
      if((king->DMAStatus & 0x1) && king->DMARetirePending)
      {
       king->DMARetirePending = FALSE;
       king->DMATransferSize = 0;
       king->DMAInterrupt = TRUE;
       RedoKINGIRQCheck();
       king->DMAStatus &= ~1;
      }
      else if(!king->DRQ)
      {
       king->DRQ = TRUE;
       king->data_cache = SCSICD_GetDB();
       //SCSICD_SetACK(TRUE);
       //PCFX_SetEvent(PCFX_EVENT_SCSI, SCSICD_Run(timestamp));

       if(king->DMAStatus & 0x1)
       {
        kbus_event_ts = running_timestamp;
        /* A complete SCSI halfword cannot be ACKed until the KING K-BUS
         * accepts its scheduled write. Leaving REQ asserted applies
         * protocol backpressure instead of the old eager-write fallback. */
        if(DoRealDMA(king->data_cache))
        {
         king->DRQ = FALSE;
         SCSICD_SetACK(TRUE);
         scsicd_ne = SCSICD_Run(running_timestamp);
        }
        else
        {
         /* REQ remains asserted; retry this same byte on the next DMA
          * clock after outstanding bus transactions have retired. */
         king->DRQ = FALSE;
        }
       }
       else
       {
        CDXFERDBG("t=%u pump: DRQ gap-latch (DMAStatus clear, byte held)\n",
                  (unsigned)running_timestamp);
       }
      }
      else if(!(king->DMAStatus & 0x1))
      {
       /* one-shot would be ideal; rate isn't a problem in practice since
        * REQ only re-fires per byte */
       CDXFERDBG("t=%u pump: REQ up but DRQ already latched, DMAStatus clear -- stalled\n",
                 (unsigned)running_timestamp);
      }
     }
     else if(SCSICD_GetACK() && !SCSICD_GetREQ())
     {
      SCSICD_SetACK(FALSE);
      scsicd_ne = SCSICD_Run(running_timestamp);
     }
    }
   }
   else if(king->dma_send_active)
   {
    if(!SCSICD_GetIO())
    {
     if(SCSICD_GetREQ() && !SCSICD_GetACK())
     {
      if(!king->DRQ)
      {
       //KINGDBG("Did write: %02x\n", king->data_cache);
       SCSICD_SetDB(king->data_cache);
       SCSICD_SetACK(TRUE);
       scsicd_ne = SCSICD_Run(running_timestamp);
       king->DRQ = TRUE;
      }
     }
     else if(SCSICD_GetACK() && !SCSICD_GetREQ())
     {
      SCSICD_SetACK(FALSE);
      scsicd_ne = SCSICD_Run(running_timestamp);
     }
    }
   }
  }
 } // end while(clocks > 0)

 return(timestamp + CalcNextExternalEvent(0x4FFFFFFF));
}

/* HuC6272 C6272_2 §3.4.1/3.4.2: R41/R43 settings written during
   a transfer take effect only when its current 16-raster block has ended.
   R40 enable and R42 raster timing remain immediate per register table. */
static void KING_RainbowCommitPending(void)
{
 if(king->RAINBOWConfigPending & 1u)
  king->RAINBOWActiveKRAMA = king->RAINBOWKRAMA;
 if(king->RAINBOWConfigPending & 2u)
  king->RAINBOWActiveBlockCount = king->RAINBOWTransferBlockCount;
 king->RAINBOWConfigPending = 0;
}

static void KING_RainbowWriteSourceAddress(uint32 addr)
{
 king->RAINBOWKRAMA = addr & 0x3FFFFu;
 if(king->RAINBOWBusyCount > 0)
  king->RAINBOWConfigPending |= 1u;
 else
 {
  king->RAINBOWActiveKRAMA = king->RAINBOWKRAMA;
  king->RAINBOWConfigPending &= (uint8)~1u;
 }
}

static void KING_RainbowWriteBlockCount(uint16 count)
{
 king->RAINBOWTransferBlockCount = count & 0x1Fu;
 if(king->RAINBOWBusyCount > 0)
  king->RAINBOWConfigPending |= 2u;
 else
 {
  king->RAINBOWActiveBlockCount = king->RAINBOWTransferBlockCount;
  king->RAINBOWConfigPending &= (uint8)~2u;
 }
}

/* HuC6272 C6272_1 register 50 / C6272_2 §3.5.4:
   READ EN = 0 stops KRAM->HuC6230 transfer immediately, while new READ EN
   enables begin on the following -HSYNC. Rates are only specified to be
   changed while both channels are disabled; invalid combinations are not
   silently reinterpreted as a supported hardware mode. */
static void KING_WriteADPCMTransferControl(uint16 value)
{
 const uint16 written = value & 0xFu;
 const uint16 old = king->ADPCMControl;
 king->ADPCMControlWritten = written;
 /* An explicit disable invalidates the old in-flight input request. The
  * automatic end-address disable in KING_AdvanceADPCMAddress does not. */
 for(unsigned ch=0;ch<2;ch++)
  if(!(written & (1u<<ch))) king->ADPCMGrantPending[ch]=0;
 /* Clear disabled channels NOW; retain enabled channels while still set. */
 king->ADPCMControl = (written & 0xCu) | ((old & written) & 3u);
 king->ADPCMPendingEnable = (uint8)((written & 3u) & ~old);
 if(king->ADPCMControl != old)
  SoundBox_SetKINGADPCMControl(king->ADPCMControl);
}

static void KING_ADPCMEnableAtHSync(void)
{
 const uint16 pending = king->ADPCMPendingEnable & 3u;
 king->ADPCMPendingEnable = 0;
 if(!pending) return;
 for(unsigned ch = 0; ch < 2; ch++)
 {
  if((pending & (1u << ch)) && !(king->ADPCMControl & (1u << ch)))
   king->ADPCMPlayAddress[ch] = (uint32)king->ADPCMSAL[ch] << 8;
 }
 king->ADPCMControl |= pending;
 SoundBox_SetKINGADPCMControl(king->ADPCMControl);
}

uint16 KING_Read16(const v810_timestamp_t timestamp, uint32 A)
{
 int msh = A & 2;
 uint16 ret = 0;

 KING_Update(timestamp);

 //printf("KRead16: %08x, %d; %04x\n", A, timestamp, king->AR);

 switch(A & 0x704)
 {
  case 0x600:   // ?CDSRP??  AAAAAAAA
		// C = 0x4000, SCSI interrupt
		// D = 0x2000, DMA IRQ
		// S = 0x1000, CD Subchannel IRQ?
		// R = 0x0800, Raster IRQ
		// P = 0x0400, ADPCM IRQ

	      if(!msh)
	      {
	       ret = king->AR;

	       if(king->ADPCMIRQPending)
		ret |= 0x400;

	       if(king->SubChannelInterrupt)
		ret |= 0x1000;
	
	       // Gaaah, this is probably a hack...Anime Freak FX Vol 4 gets confused and crashes
	       // if both bits are set at once.
	       if(king->DMAInterrupt && (king->DMAStatus & 0x2))
		ret |= 0x2000;
	       else if(king->CDInterrupt)
		ret |= 0x4000;

	       if(king->RasterIRQPending)
		ret |= 0x800;

	       king->SubChannelInterrupt = FALSE;
               king->RasterIRQPending = FALSE;
               RedoKINGIRQCheck();
	      }
	      else
	      {
		ret |= SCSICD_GetSEL() ? 0x02: 0x00;
		ret |= SCSICD_GetIO() ? 0x04 : 0x00;
		ret |= SCSICD_GetCD() ? 0x08 : 0x00;
		ret |= SCSICD_GetMSG() ? 0x10 : 0x00;
		ret |= SCSICD_GetREQ() ? 0x20 : 0x00;
		ret |= SCSICD_GetBSY() ? 0x40 : 0x00;
		ret |= SCSICD_GetRST() ? 0x80 : 0x00;

		ret |= king->SubChannelBuf << 8;
	      }
	      break; // status...

  case 0x604: KP_REGR(king->AR); switch(king->AR)
	      {
		default:
			KINGDBG("Unknown 16-bit register read: %02x\n", king->AR);
			break;

		case 0x00:
			ret = SCSICD_GetDB();
			/* CPU-PIO SCSI read failure model: the CPU reading the SCSI
			 * data register directly, while in DATA_IN and no KING real-DMA
			 * is active, IS the structural signature of a CPU-PIO transfer
			 * (the real-DMA pump drains the FIFO through its own hardware
			 * handshake and never touches this register).  Latch it; a
			 * sector boundary in scsicd.c's RunCDRead() checks the latch
			 * and a hot timer IRQ to decide whether to fail the *next*
			 * sector of this transfer.  See KING_PIOTransferShouldFail()
			 * and docs/king-pio-read-erratum.md. */
			if(!king->dma_receive_active &&
			   SCSICD_GetIO() && !SCSICD_GetCD() && !SCSICD_GetMSG())
			{
				if(!king->PIOReadSeen)
					CDXFERDBG("CPU-PIO data read detected (transfer method latched) timer_hot=%d\n",
					          (int)king_pio_timer_irq_hot());
				king->PIOReadSeen = TRUE;
			}
			break;

		case 0x01:
			ret = REGGETHW(king->Reg01, msh);
			break;

		case 0x02:
			ret = REGGETHW(king->Reg02, msh);
			break;

		case 0x03:
			ret = REGGETHW(king->Reg03, msh);
			break;

		case 0x04:
			if(!msh)
			{
          	         ret |= SCSICD_GetSEL() ? 0x02: 0x00;
                	 ret |= SCSICD_GetIO() ? 0x04 : 0x00;
	                 ret |= SCSICD_GetCD() ? 0x08 : 0x00;
	                 ret |= SCSICD_GetMSG() ? 0x10 : 0x00;
	                 ret |= SCSICD_GetREQ() ? 0x20 : 0x00;
	                 ret |= SCSICD_GetBSY() ? 0x40 : 0x00;
	                 ret |= SCSICD_GetRST() ? 0x80 : 0x00;
			}
			break;

		case 0x05:
			if(king->Reg01 & 0x80)
                        {
			 ret = 0x00;
		         break;
			}

		        if(msh)
			{
			 ret = king->data_cache;
			 //printf("Fooball: %02x\n", ret);
			 if(king->dma_receive_active)
			 {
			  king->DRQ = FALSE;
     			  SCSICD_SetACK(TRUE);
     			  scsicd_ne = 1;
			 }
			}
			else
			{
			 ret |= SCSICD_GetACK() ? 0x01 : 0x00;
			 ret |= SCSICD_GetATN() ? 0x02 : 0x00;

			 if(king->dma_receive_active || king->dma_send_active)
			  if(king->DRQ)
 			   ret |= 0x40;
			
		         // Gaaah, this is probably a hack...Anime Freak FX Vol 4 gets confused and crashes
	                 // if both bits are set at once.
			 if(!king->DMAInterrupt)
			  ret |= king->CDInterrupt ? 0x10 : 0x00;

                         if(SCSICD_GetIO() == ((king->Reg03 >> 0) & 1) && 
			    SCSICD_GetCD() == ((king->Reg03 >> 1) & 1) && 
			    SCSICD_GetMSG() == ((king->Reg03 >> 2) & 1))
                         {
               	          ret |= 0x8;	// Phase match
                         }
			}
			break;

		case 0x06: // SCSI Input Data Register, same value returned as reading D16-D23 of register 0x05?
			KINGDBG("Input data for...?\n");
			ret = king->data_cache;
			break;

		case 0x07: 
			// SCSI IRQ acknowledge/reset
			KINGDBG("SCSI IRQ acknowledge\n");
			king->CDInterrupt = FALSE;
                        RedoKINGIRQCheck();
			ret = 0xFF;
			break;

		case 0x08: 	// Sub-channel data
			if(!msh)
			{
			 ret = king->SubChannelBuf;
			 king->SubChannelBuf = 0;
			 //puts("Sub-channel data read.");
			}
			break;

                case 0x09: 
			ret = REGGETHW(king->DMATransferAddr, msh);
			break;

                case 0x0A: 
			ret = REGGETHW(king->DMATransferSize, msh);
			break;

		case 0x0B: // Value read in the BIOS always seems to be discarded...  DMA IRQ acknowledge?
			if(!msh)
			{
			 ret = king->DMAInterrupt ? 1 : 0;
                         KINGDBG("DMA IRQ Acknowledge: %d\n", ret);
                         king->DMAInterrupt = 0;
                         RedoKINGIRQCheck();
			}
			break;

		case 0x0C: 
			ret = REGGETHW(king->KRAMRA, msh);
			break;

		case 0x0D:
			ret = REGGETHW(king->KRAMWA, msh);
			break;

		case 0x0E:
			{
                         unsigned int page = KING_KRAMPage((king->KRAMRA & 0x80000000) ? 1 : 0);
                         int32 inc_amount = ((int32)((king->KRAMRA & (0x3FF << 18)) << 4)) >> 22; // Convert from 10-bit signed 2's complement 

                         KP_CPU_RD(page, king->KRAMRA & 0x3FFFF);
                         ret = king->KRAM[page][king->KRAMRA & 0x3FFFF];

	
                         king->KRAMRA = (king->KRAMRA &~ 0x1FFFF) | ((king->KRAMRA + inc_amount) & 0x1FFFF);
                        }
			break;

		case 0x0F: ret = king->PageSetting;
			   break;

                case 0x10: ret = REGGETHW(king->bgmode, msh);
			   break;

		case 0x15: ret = king->MPROGControl;
			   break;

		case 0x61: ret = king->KRAM_Mode;
			   break;

		//case 0x40: break; 	// Super Power League FX reads this, but I think it's write-only.

		case 0x53:
			  {
			   ret = king->ADPCMStatus[0] | (king->ADPCMStatus[1] << 2);

			   king->ADPCMStatus[0] = king->ADPCMStatus[1] = 0;
			   king->ADPCMIRQPending = 0;

			   RedoKINGIRQCheck();

			   ADPCMDBG("Status read: %02x\n", ret);
			  }
			  break;
	      }
	      break;
	      
 }

 PCFX_SetEvent(PCFX_EVENT_KING, timestamp + CalcNextExternalEvent(0x4FFFFFFF));    // TODO: Optimize this to only be called when necessary.

 return(ret);
}

void KING_Write8(const v810_timestamp_t timestamp, uint32 A, uint8 V)
{
 KING_Write16(timestamp, A & 0x706, V << ((A & 1) ? 8 : 0));
}

static inline void SCSI_Reg0_Write(uint8 V, bool delay_run)
{
 king->Reg00 = V;
 SCSICD_SetDB(V);

 KINGDBG("WriteDB: %02x\n", V);

 if(!delay_run)
 {
  scsicd_ne = 1; //SCSICD_Run(timestamp);
 }
}

static inline void SCSI_Reg2_Write(uint8 V, bool delay_run)
{
 KINGDBG("SCSI Mode: %04x\n", V);

 /* SCSI Mode Register
                               D0 = SED:  When using with sequential DMA mode, you use.
                                          (It sets the normal DMA mode time to "0".)
                                        Sequential DMA, the number of transfer data bytes which are set to the SCSI device (m) HuC6272 (REG.A) the number
                                        of transfer bytes (n) it sets more largely, abbreviates the status message command phase after the n byte
                                        transferring and being something which makes the transfer after the n + 1 byte possible, it is possible to
                                        increase the transfer performance from the slow SCSI device of CD-ROM and the like.
                                        Sequential DMA cannot use with imitation DMA. When this bit is designated as 1, because -ACK the signal
                                        mandatorily ネゲート it is done, other than the stipulated sequence please do not use.

                               D1 = DMA Mode:
                                        When using the SCSI control section with normal DMA mode, "1" is set.
                           */
 if(!(V & 0x2) && (king->Reg02 & 0x2))
 {

  { // HACK(probably)
   king->CDInterrupt = FALSE;
   RedoKINGIRQCheck();
  }

  SCSICD_SetACK(0);

  if(!delay_run)
  {
   scsicd_ne = 1; //SCSICD_Run(timestamp);
  }
  king->DRQ = FALSE;

  king->dma_receive_active = FALSE;
  king->dma_send_active = FALSE;
  king->dma_cycle_counter = 0x7FFFFFFF;

  CDXFERDBG("reg2 DMA-mode 1->0: pump disarmed, DRQ cleared\n");
 }

 king->Reg02 = V;
}

static inline void SCSI_Reg3_Write(uint8 V, bool delay_run)
{
 KINGDBG("Set phase match SCSI bus bits: IO: %d, CD: %d, MSG: %d\n", (int)(bool)(V & 1), (int)(bool)(V & 2), (int)(bool)(V & 4));
 king->Reg03 = V & 0x7;

 if(!delay_run)
 {
  scsicd_ne = 1; //SCSICD_Run(timestamp);
 }
}

/* The aggregate empirical KRAM contention fit has been retired. The
 * single supported path is the per-bank posted-write drain scheduler.
 * Historical throughput measurements are preserved in docs, not code. */

/* Optional, observational-only KING bus timing ledger.  These counters do not
 * affect the simulated bus, CPU timestamps, or any guest-visible state.  The
 * phase bins are V810-clock modulo 8 (NOT calibrated K-BUS access slots). */
#ifdef V810_PROFILE
static uint64_t kbt_write_count, kbt_posted_waits, kbt_posted_cycles;
static uint64_t kbt_contention_cycles, kbt_contention_writes;
static uint64_t kbt_read_count, kbt_read_cycles;
static uint64_t kbt_cpu_phase[8], kbt_wait_hist[9];
static uint64_t kbt_slot_writes[17], kbt_slot_cycles[17];
static uint64_t kbt_wide_writes, kbt_narrow_writes, kbt_idle_writes;
static uint64_t kbt_active_writes, kbt_hblank_writes;
static uint64_t kbt_busy_duration_max;
static uint64_t kbt_cpu_bank_writes[2];
static uint64_t kbt_micro_bank_hist[2][9];
/* 3.6.7: 8 successive KING 5.37MHz dot slots per independently serviced
 * KRAM bank.  The ACTIVE phase starts at the model's 1024-clock window;
 * the true silicon-to-V810 bus phase offset is NOT known.  Counts below
 * observe the programmed 8-slot sequence and never modify scheduling. */
static uint64_t kbt_phase_writes[2][8];
static uint64_t kbt_phase_nonnop[2][8];
static uint64_t kbt_phase_visible[2][8];
static uint64_t kbt_phase_hblank[2];
static uint64_t kbt_phase_disabled_mprog[2];

/* The guest V810 PC is recorded at dispatch, before each I/O write.
 * The 32-byte buckets are observational only: no timing changes. */
static uint64_t kbt_pc_write_count[V810_PROF_NBUCKET];
static uint64_t kbt_pc_latch_wait[V810_PROF_NBUCKET];
static uint64_t kbt_pc_contention[V810_PROF_NBUCKET];
static unsigned kbt_source_bucket(void)
{
 return v810p_prev_pc < V810_PROF_RAMSIZE ?
     v810p_prev_pc >> V810_PROF_BSHIFT : V810_PROF_NBUCKET;
}


static void kbt_report(unsigned long long frames)
{
 const double f = frames ? (double)frames : 1.0;
 fprintf(stderr,"\n==== KING K-BUS / CPU STALL ATTRIBUTION (modeled; NOT hardware measurement) ====\n");
 fprintf(stderr,"KRAM data writes/field: %.2f; display=%llu blank=%llu\n",kbt_write_count/f,
     (unsigned long long)kbt_active_writes,(unsigned long long)kbt_hblank_writes);
 fprintf(stderr,"Posted-latch waits: %llu (%.2f/field); cycles: %llu (%.2f/field), max wait=%llu\n",
    (unsigned long long)kbt_posted_waits,kbt_posted_waits/f,
    (unsigned long long)kbt_posted_cycles,kbt_posted_cycles/f,
    (unsigned long long)kbt_busy_duration_max);
 fprintf(stderr,"Modeled microprogram contention: %llu cycles (%.2f/field), charged writes=%llu\n",
    (unsigned long long)kbt_contention_cycles,kbt_contention_cycles/f,
    (unsigned long long)kbt_contention_writes);
 fprintf(stderr,"KRAM data reads: %llu (%.2f/field); read wait: %llu cycles (%.2f/field)\n",
    (unsigned long long)kbt_read_count,kbt_read_count/f,
    (unsigned long long)kbt_read_cycles,kbt_read_cycles/f);
 fprintf(stderr,"Microprogram load classes: idle=%llu narrow=%llu wide=%llu\n",
    (unsigned long long)kbt_idle_writes,(unsigned long long)kbt_narrow_writes,(unsigned long long)kbt_wide_writes);
 fprintf(stderr,"K-BUS pressure by active microprogram slots (count: writes / charged cycles):\n");
 for(int n=0;n<=16;n++) if(kbt_slot_writes[n])
    fprintf(stderr,"  %2d slots: %10llu / %10llu\n",n,
      (unsigned long long)kbt_slot_writes[n],(unsigned long long)kbt_slot_cycles[n]);
 fprintf(stderr,"KRAM write bank A=%llu, bank B=%llu; active BG fetch-slot count by bank (8-slot group):\n",
     (unsigned long long)kbt_cpu_bank_writes[0], (unsigned long long)kbt_cpu_bank_writes[1]);
 for(int b=0;b<2;b++) {
  fprintf(stderr,"  bank %c:",'A'+b);
  for(int n=0;n<=8;n++) if(kbt_micro_bank_hist[b][n])
     fprintf(stderr," %d:%llu",n,(unsigned long long)kbt_micro_bank_hist[b][n]);
  fprintf(stderr,"\n");
 }
 fprintf(stderr,"KING active-display microprogram phase (observational; phase offset uncalibrated):\n");
 fprintf(stderr,"  bank  slot    CPU_writes   nonNOP_at_slot   enabled_BG_at_slot\n");
 for(int b=0;b<2;b++) for(int slot=0;slot<8;slot++)
  fprintf(stderr,"  %c     %d     %10llu %16llu %20llu\n",'A'+b,slot,
    (unsigned long long)kbt_phase_writes[b][slot],
    (unsigned long long)kbt_phase_nonnop[b][slot],
    (unsigned long long)kbt_phase_visible[b][slot]);
 fprintf(stderr,"  HBlank writes: bank A=%llu, B=%llu; MPSW-off active writes: A=%llu B=%llu\n",
    (unsigned long long)kbt_phase_hblank[0],
    (unsigned long long)kbt_phase_hblank[1],
    (unsigned long long)kbt_phase_disabled_mprog[0],
    (unsigned long long)kbt_phase_disabled_mprog[1]);
 const char *phase_csv = getenv("PCFX_KBUS_PHASE_CSV");
 if(phase_csv && *phase_csv)
 {
  FILE *fp = fopen(phase_csv, "w");
  if(fp)
  {
   fprintf(fp,"bank,slot,issued_writes,programmed_non_nop,enabled_bg_slot,phase_origin\n");
   for(int b=0;b<2;b++) for(int slot=0;slot<8;slot++)
    fprintf(fp,"%c,%d,%llu,%llu,%llu,active_window_start\n",'A'+b,slot,
      (unsigned long long)kbt_phase_writes[b][slot],
      (unsigned long long)kbt_phase_nonnop[b][slot],
      (unsigned long long)kbt_phase_visible[b][slot]);
   fclose(fp);
   fprintf(stderr,"Wrote K-BUS microprogram-phase histogram: %s\n",phase_csv);
  }
 }
 fprintf(stderr,"CPU issue clock phase (mod 8; NOT a calibrated K-BUS phase):");
 for(int i=0;i<8;i++) fprintf(stderr," %llu",(unsigned long long)kbt_cpu_phase[i]);
 fprintf(stderr,"\nPosted-write wait histogram, bins 0..7, >=8 cycles:");
 for(int i=0;i<9;i++) fprintf(stderr," %llu",(unsigned long long)kbt_wait_hist[i]);
 /* Attribute KRAM write costs to the calling V810 instruction, not an
  * assumed symbol/guest. This makes inlined framebuffer clears visible. */
 fprintf(stderr,"\nKRAM writer hot PCs (32-byte buckets, most modeled stall cycles):\n");
 fprintf(stderr,"%-10s %12s %14s %14s\n", "PC", "writes", "latch_wait", "contention");
 unsigned selected[16]; unsigned nsel=0;
 for(unsigned rank=0;rank<16;rank++)
 {
  unsigned best=V810_PROF_NBUCKET; uint64_t top=0;
  for(unsigned i=0;i<V810_PROF_NBUCKET;i++)
  {
   const uint64_t cost=kbt_pc_latch_wait[i]+kbt_pc_contention[i];
   if(cost<=top) continue;
   int seen=0; for(unsigned j=0;j<nsel;j++) if(selected[j]==i) {seen=1;break;}
   if(!seen) {best=i;top=cost;}
  }
  if(best==V810_PROF_NBUCKET) break;
  selected[nsel++]=best;
  fprintf(stderr,"0x%06x %12llu %14llu %14llu\n",
      best<<V810_PROF_BSHIFT,
      (unsigned long long)kbt_pc_write_count[best],
      (unsigned long long)kbt_pc_latch_wait[best],
      (unsigned long long)kbt_pc_contention[best]);
 }
 const char *csv=getenv("PCFX_KBUS_PC_CSV");
 if(csv && *csv)
 {
  FILE *fp=fopen(csv,"w");
  if(fp) {
   fprintf(fp,"pc_bucket,writes,latch_wait_cycles,contention_cycles\n");
   for(unsigned i=0;i<V810_PROF_NBUCKET;i++) if(kbt_pc_write_count[i])
    fprintf(fp,"0x%06x,%llu,%llu,%llu\n",i<<V810_PROF_BSHIFT,
      (unsigned long long)kbt_pc_write_count[i],
      (unsigned long long)kbt_pc_latch_wait[i],
      (unsigned long long)kbt_pc_contention[i]);
   fclose(fp);
   fprintf(stderr,"Wrote KING write-PC histogram: %s\n",csv);
  }
 }
 fprintf(stderr,"\nCAUTION: uncalibrated grant phase and missing cross-device bus-service slots\n"
                "mean these cycles cannot be interpreted as a physical arbitration schedule.\n");
}
#endif


/* HuC6272 §§3.1–3.2: one CPU write latch; a new write waits until the
 * previous one drains. This is now the sole timing path.
 * Source-defined request categories are tracked below; exact silicon phase
 * and request-to-grant latency are not in the manual. */
static v810_timestamp_t KING_KBusGrantDeadline(v810_timestamp_t accepted,
                                               v810_timestamp_t now,
                                               unsigned bank, unsigned required_grants)
{
 /* Each KING dot lasts four V810 clocks. Estimate completion after four
  * grantable KING dots. HPhaseCounter is the remaining active-picture
  * clock count at `now`, as updated by KING_Update(). */
 const bool active = HPhase == HPHASE_ACTIVE && !fx_vce.in_hblank;
 const uint32 remaining = active && HPhaseCounter > 0 ? (uint32)HPhaseCounter : 0u;
 const uint32 elapsed = active && remaining <= 1024u ? 1024u - remaining : 0u;
 const uint32 start = (uint32)(accepted - now);
 uint32 delta = start;
 unsigned grants = 0;
 /* Advance to a KING-dot boundary on the current active-line clock grid.
  * Outside active video no BG fetch slots are reserved in this model. */
 if(active)
  delta += (4u - ((elapsed + start) & 3u)) & 3u;
 else
  delta += (4u - (start & 3u)) & 3u;
 for(unsigned guard = 0; guard < 512 && grants < required_grants; guard++, delta += 4)
 {
  bool bg_request = false;
  if(active && delta < remaining && (king->MPROGControl & 1u))
  {
   const unsigned slot = ((elapsed + delta) / 4u) & 7u;
   const uint16 mp = king->MPROGData[(bank & 1u) * 8u + slot];
   if(!(mp & 0x100u))
   {
    const unsigned bg = (mp >> 6) & 3u;
    bg_request = ((king->priority >> (bg * 3u)) & 7u) != 0;
   }
  }
  if(!bg_request)
  {
   const uint32 grant_time = now + delta;
   const unsigned i = (grant_time / 4u) & (KING_KBUS_SLOTS - 1u);
   const bool peripheral = king->KBusSlotSource[bank & 1u][i] &&
      king->KBusSlotTime[bank & 1u][i] == grant_time;
   if(!peripheral) grants++;
  }
 }
 /* Last grant consumes the dot interval, not merely its leading edge. */
 return now + delta;
}

static v810_timestamp_t KING_KRAMSlotDrainDeadline(v810_timestamp_t accepted,
                                                      v810_timestamp_t now, unsigned bank)
{
 return KING_KBusGrantDeadline(accepted, now, bank, 4);
}

/* A KRAM write enters KING's input latch and releases the CPU while that
 * halfword drains.  Hardware observations put the complete back-to-back
 * OUT.H interval near 16 V810 clocks.  The V810 core and I/O handler have
 * already charged four clocks by the time this callback sees the write, so
 * 12 remain in this timestamp domain.  A second write before then holds the
 * CPU in WAIT until the latch is free. */

uint32 KING_KRAMWriteWaitCycles(const v810_timestamp_t timestamp, uint32 A)
{
 v810_timestamp_t ready;
 uint32 wait = 0;

 if(!(A & 0x4) || king->AR != 0x0E)
  return 0;

 KING_Update(timestamp);
 ready = king->KRAMWriteBusyUntil;
 if(ready > timestamp)
  wait = (uint32)(ready - timestamp);

 king->KRAMWriteBusyUntil = KING_KRAMSlotDrainDeadline(timestamp + wait, timestamp,
                                                     KING_KRAMPage((king->KRAMWA >> 31) & 1u));
#ifdef V810_PROFILE
 kbt_write_count++;
 { unsigned b=kbt_source_bucket();
   if(b<V810_PROF_NBUCKET) { kbt_pc_write_count[b]++; kbt_pc_latch_wait[b]+=wait; }
 }
 kbt_posted_cycles += wait;
 if(wait) kbt_posted_waits++;
 if(wait > kbt_busy_duration_max) kbt_busy_duration_max = wait;
 kbt_wait_hist[(wait < 8) ? wait : 8]++;
 kbt_cpu_phase[(unsigned)timestamp & 7u]++;
 kbt_cpu_bank_writes[KING_KRAMPage((king->KRAMWA >> 31) & 1u)]++;
 {
  const unsigned bank = KING_KRAMPage((king->KRAMWA >> 31) & 1u);
  if(fx_vce.in_hblank || HPhase != HPHASE_ACTIVE)
   kbt_phase_hblank[bank]++;
  else if(!(king->MPROGControl & 1u))
   kbt_phase_disabled_mprog[bank]++;
  else
  {
   /* HPhaseCounter counts *remaining master clocks* in the active
    * 1024-clock picture interval; one KING dot = four clocks. */
   const unsigned elapsed = HPhaseCounter >= 1024 ? 0u :
                            1024u - (unsigned)HPhaseCounter;
   const unsigned slot = (elapsed / 4u) & 7u;
   const uint16 mp = king->MPROGData[bank * 8u + slot];
   kbt_phase_writes[bank][slot]++;
   if(!(mp & 0x100u))
   {
    kbt_phase_nonnop[bank][slot]++;
    if((king->priority >> (((mp >> 6) & 3u) * 3u)) & 7u)
     kbt_phase_visible[bank][slot]++;
   }
  }
 }

 {
  int per_bank[2] = {0,0};
  if(king->MPROGControl & 1)
   for(int i=0;i<16;i++) if(!(king->MPROGData[i] & 0x100)) {
    int bg=(king->MPROGData[i] >> 6) & 3;
    if((king->priority >> (bg * 3)) & 7u) per_bank[i >> 3]++;
   }
  for(int b=0;b<2;b++) kbt_micro_bank_hist[b][per_bank[b]]++;
 }
 if(fx_vce.in_hblank) kbt_hblank_writes++; else kbt_active_writes++;
#endif
 return wait;
}

/* HuC6272 §3.1.2: a CPU KRAM data read waits for an outstanding
 * posted access, then joins the same per-bank arbitration as a writer.
 * A read consumes one model KING-dot grant. The final one-clock return
 * overhead retains the earlier approximate free-bus five-clock total;
 * the manual does not specify that return overhead. */
uint32 KING_KRAMReadWaitCycles(v810_timestamp_t timestamp, uint32 A)
{
 if(!(A & 0x4) || king->AR != 0x0E) return 0;
 KING_Update(timestamp);
 const unsigned bank=KING_KRAMPage((king->KRAMRA >> 31) & 1u);
 const v810_timestamp_t ready=king->KRAMWriteBusyUntil > timestamp ?
                                 king->KRAMWriteBusyUntil : timestamp;
 const v810_timestamp_t completed=KING_KBusGrantDeadline(ready,timestamp,bank,1);
 const uint32 wait=(uint32)(completed-timestamp+1);
#ifdef V810_PROFILE
 kbt_read_count++;
 kbt_read_cycles += wait;
#endif
 return wait;
}

static void KING_RefreshBGModeAtCurrentDot(void);

void KING_Write16(const v810_timestamp_t timestamp, uint32 A, uint16 V)
{
 int msh = A & 0x2;

 //printf("Write16: %08x %04x\n", A, V);


 if(!(A & 0x4))
 {
  if(!msh) 
   king->AR = V & 0x7F; 
 }
 else
 {
  //if(king->AR != 0x0E)
  // printf("KING: %02x %04x, %d\n", king->AR, V, fx_vce.raster_counter);
  KING_Update(timestamp);

  if(king->AR >= 0x50 && king->AR <= 0x5E)
  {
   //ADPCMDBG("Write: %02x(%d), %04x", king->AR, msh, V);
  }

  KP_REGW(king->AR);

	      switch(king->AR)
	      {
		default: 
			KINGDBG("Unknown 16-bit register write: %02x %04x %d\n", king->AR, V, msh); 
			break;

		case 0x00: if(king->Reg01 & 0x80)
			    break;

			   if(!msh) 
			   {
			    SCSI_Reg0_Write(V, false);
			   }
			   break;

		case 0x01: if(!msh)
			   {
                            KINGDBG("Set SCSI BUS bits; Assert DB: %d, ATN: %d, SEL: %d, ACK: %d, RST: %d, %02x\n",
                                (int)(bool)(V & 1), (int)(bool)(V & 2), (int)(bool)(V & 4),
                                (int)(bool)(V & 0x10), (int)(bool)(V &0x80), SCSICD_GetDB());

			    if(V & 0x80)	// RST, silly KING, resets SCSI internal control registers too!
			    {
			     if(!(king->Reg01 & 0x80))
			     {
			      SCSI_Reg0_Write(0, TRUE);
			      SCSI_Reg2_Write(0, TRUE);
			      SCSI_Reg3_Write(0, TRUE);
			      king->data_cache = 0x00;
			      king->PIOReadSeen = FALSE;   /* a full bus reset clears the PIO-fail latch */
			      king->PIOBusWedged = FALSE;  /* ...and unpoisons the wedged bus -- the one
			                                    * recovery probe 027 proved works on hardware */

			      //king->CDInterrupt = true;
			      //RedoKINGIRQCheck();
			      //puts("KING RST IRQ");
			     }

			     king->Reg01 = V & 0x80; // Only this bit remains...how lonely.
			    }
			    else
			    {
			     king->Reg01 = V & (1 | 2 | 4 | 0x10 | 0x80);

			     SCSICD_SetATN(V & 2);
			     SCSICD_SetSEL(V & 4);
			     SCSICD_SetACK(V & 0x10);
			    }
                            SCSICD_SetRST(V & 0x80);
			    scsicd_ne = 1;
			   }
			   break;

		case 0x02: if(king->Reg01 & 0x80)
                            break;

			   if(!msh)
			   {
			    SCSI_Reg2_Write(V, false);
			   }
			   break;

		case 0x03: if(king->Reg01 & 0x80)
                            break;

			   if(!msh)
			   {
			    SCSI_Reg3_Write(V, false);
			   }
			   break;

		case 0x05: if(king->Reg01 & 0x80)
                            break;

			   if(!msh)	// Start DMA target receive
			   {
                            KINGDBG("DMA target receive: %04x, %d\n", V, msh);
                            king->dma_receive_active = FALSE;
                            king->dma_send_active = TRUE;
			    king->DRQ = TRUE;
			    //StartKingMagic();
			    king->dma_cycle_counter = KING_MAGIC_INTERVAL;
			   }
			   else
			   {
			    if(king->dma_send_active && king->DRQ)
			    {
			     //KINGDBG("%02x\n", V);
			     king->data_cache = V;
			     king->DRQ = FALSE;
			    }
			   }
			   break;

		case 0x06: break; // Not used for writes?

		case 0x07: if(king->Reg01 & 0x80)
                            break;

			   KINGDBG("Start DMA initiator receive: %04x\n", V);

			   if(king->Reg02 & 0x2)
			   {
			    king->DMACountBounded = (king->DMATransferSize != 0);
			    king->dma_receive_active = TRUE;
			    king->dma_send_active = FALSE;
			    //StartKingMagic();
			    king->dma_cycle_counter = KING_MAGIC_INTERVAL;

			    CDXFERDBG("t=%u DMA arm: dest=%05x page=%d size=%u words (%s) timer_hot=%d\n",
			              (unsigned)timestamp,
			              king->DMATransferAddr, king->PageSetting & 1,
			              king->DMATransferSize >> 1,
			              king->DMACountBounded ? "count-bounded" : "phase-driven/count-0",
			              (int)king_pio_timer_irq_hot());
			   }
			   break;

		case 0x08: // Sub-channel control
			   KINGDBG("Sub-channel control: %02x\n", V);

			   king->SubChannelControl = V & 0x3;
			   king->SubChannelInterrupt = FALSE;
			   RedoKINGIRQCheck();
			   break;

                case 0x09: REGSETHW(king->DMATransferAddr, V, msh); king->DMATransferAddr &= 0x3FFFF; break;
                case 0x0A: REGSETHW(king->DMATransferSize, V, msh); king->DMATransferSize &= 0x3FFFE; king->DMATransferFlipFlop = 0;
			   /* Programming a new length starts a new transfer: drop any
			    * trailing-item wait left over from a previous one (e.g. an
			    * abandoned, wedged transfer that a driver is retrying), so it
			    * cannot retire this one early on its first byte. */
			   king->DMARetirePending = FALSE;
			   break;
		case 0x0B: REGSETHW(king->DMAStatus, V, msh);
			   king->DMAStatus &= 0x3;
			   king->DMAInterrupt = 0;
			   RedoKINGIRQCheck();

			   king->DMATransferFlipFlop = 0;

			   KINGDBG("SCSI DMA: %04x, dest=%06x, page=%d, size=%06x(16-bit words)\n", V, king->DMATransferAddr, king->PageSetting & 1, king->DMATransferSize >> 1);

			   break;
                case 0x0C: REGSETHW(king->KRAMRA, V, msh); break;
                case 0x0D: REGSETHW(king->KRAMWA, V, msh); break;
		case 0x0E: 
			  {
			   unsigned int page = KING_KRAMPage((king->KRAMWA & 0x80000000) ? 1 : 0);
			   int32 inc_amount = ((int32)((king->KRAMWA & (0x3FF << 18)) << 4)) >> 22; // Convert from 10-bit signed 2's complement

			   KP_CPU_WR(page, king->KRAMWA & 0x3FFFF);
                           {
                            /* KING_Update(timestamp) above has retired the
                             * preceding latch if its WAIT has elapsed. */
                            KING_CommitPendingKRAM(timestamp);
                            king->KRAMWritePendingPage = (uint8)page;
                            king->KRAMWritePendingAddress = king->KRAMWA & 0x3FFFFu;
                            king->KRAMWritePendingValue = V;
                            king->KRAMWritePendingAt = king->KRAMWriteBusyUntil;
                            king->KRAMWritePending = 1;
                           }
			   king->KRAMWA = (king->KRAMWA &~ 0x1FFFF) | ((king->KRAMWA + inc_amount) & 0x1FFFF);
			  }
			  break;


		// Page settings(0/1) for BG, DMA, ADPCM, and RAINBOW transfers.
		case 0x0F: REGSETHW(king->PageSetting, V, msh);
			   RecalcKRAMPagePtrs();
			   break;


		// Background Modes
		case 0x10: { const uint16 oldmode = king->bgmode;
                           const uint8 om=oldmode&0xF;
                           REGSETHW(king->bgmode, V, msh);
                           { const uint8 nm=king->bgmode&0xF;
                             if(nm!=om&&(nm&0x7)&&(om&0x7)) king->BG0FetchRehomed=FALSE; }
                           if(oldmode != king->bgmode) KING_RefreshBGModeAtCurrentDot();
                           break; }


		// Background priorities and affine transform master enable.
		case 0x12: if(!msh)
			   {
			    king->priority = V;
			    if(king->priority & ~0x1FFF)
			    {
			     KING_UNDEF("Invalid priority bits set: %04x\n", king->priority);
			    }
			   }
			   break;


		// Microprogram Address
		case 0x13: if(!msh)
			   {
			    king->MPROGAddress = V & 0xF;
			   }
			   break;


		// Microprogram Data Port
		case 0x14: if(!msh)
			   {
			    king->MPROGData[king->MPROGAddress] = V;
			    king->MPROGAddress = (king->MPROGAddress + 1) & 0xF;
			   }
			   break;

		case 0x15: REGSETHW(king->MPROGControl, V, msh); king->MPROGControl &= 0x1; break;

		case 0x16: REGSETHW(king->BGScrollMode, V, msh); king->BGScrollMode &= 0xF; break;

		case 0x20: REGSETHW(king->BGBATAddr[0], V, msh); break;
		case 0x21: REGSETHW(king->BGCGAddr[0], V, msh); king->BG0FetchRehomed=TRUE; break;
		case 0x22: REGSETHW(king->BG0SubBATAddr, V, msh); break;
	  	case 0x23: REGSETHW(king->BG0SubCGAddr, V, msh); break;

		case 0x24: REGSETHW(king->BGBATAddr[1], V, msh); break;
		case 0x25: REGSETHW(king->BGCGAddr[1], V, msh); break;
		case 0x28: REGSETHW(king->BGBATAddr[2], V, msh); break;
		case 0x29: REGSETHW(king->BGCGAddr[2], V, msh); break;
		case 0x2A: REGSETHW(king->BGBATAddr[3], V, msh); break;
		case 0x2B: REGSETHW(king->BGCGAddr[3], V, msh); break;

		case 0x2C: REGSETHW(king->BGSize[0], V, msh); break;
		case 0x2D: REGSETHW(king->BGSize[1], V, msh); king->BGSize[1] &= 0x00FF; break;
		case 0x2E: REGSETHW(king->BGSize[2], V, msh); king->BGSize[2] &= 0x00FF; break;
		case 0x2F: REGSETHW(king->BGSize[3], V, msh); king->BGSize[3] &= 0x00FF; break;

		case 0x30: REGSETHW(king->BGXScroll[0], V, msh); king->BGXScroll[0] &= 0x7FF; break;
		case 0x31: REGSETHW(king->BGYScroll[0], V, msh); king->BGYScroll[0] &= 0x7FF; break;

		case 0x32: REGSETHW(king->BGXScroll[1], V, msh); king->BGXScroll[1] &= 0x3FF; break;
		case 0x33: REGSETHW(king->BGYScroll[1], V, msh); king->BGYScroll[1] &= 0x3FF; break;

		case 0x34: REGSETHW(king->BGXScroll[2], V, msh); king->BGXScroll[2] &= 0x3FF; break;
		case 0x35: REGSETHW(king->BGYScroll[2], V, msh); king->BGYScroll[2] &= 0x3FF; break;

		case 0x36: REGSETHW(king->BGXScroll[3], V, msh); king->BGXScroll[3] &= 0x3FF; break;
		case 0x37: REGSETHW(king->BGYScroll[3], V, msh); king->BGYScroll[3] &= 0x3FF; break;


		case 0x38: REGSETHW(king->BGAffinA, V, msh); break;
		case 0x39: REGSETHW(king->BGAffinB, V, msh); break;
		case 0x3a: REGSETHW(king->BGAffinC, V, msh); break;
		case 0x3b: REGSETHW(king->BGAffinD, V, msh); break;
		case 0x3c: REGSETHW(king->BGAffinCenterX, V, msh); break;
		case 0x3d: REGSETHW(king->BGAffinCenterY, V, msh); break;


		case 0x40: // ------IE
			   // I = 1, interrupt enable??
			   // E = 1, rainbow transfer enable
			   if(!msh)
			   {
			    king->RAINBOWTransferControl = V & 0x3;
			    if(!(V & 1))
			    {
			     //if(king->RAINBOWBusyCount || king->RAINBOWBlockCount)
			     // puts("RAINBOW transfer reset");
			     // Not sure if this is completely correct or not.  Test cases: "Tonari no Princess Rolfee", (others?)
			     //king->RAINBOWBusyCount = 0;
			     //king->RAINBOWBlockCount = 0;
			     //RAINBOW_ForceTransferReset();
			     king->RAINBOWBlockCount = 0;
			    }
			   }
			   king->RasterIRQPending = FALSE;
			   RedoKINGIRQCheck();
                           //printf("Transfer Control: %d, %08x\n", fx_vce.raster_counter,  king->RAINBOWTransferControl);
			   break;

		// Rainbow transfer address
                case 0x41: { uint32 next = king->RAINBOWKRAMA;
                           REGSETHW(next, V, msh);
                           KING_RainbowWriteSourceAddress(next);
                           }
			   //printf("KRAM Transfer Addr: %d, %08x\n", fx_vce.raster_counter,  king->RAINBOWKRAMA);
			   break;

		// 0-262
		case 0x42: if(!msh) 
			   {
			    king->RAINBOWTransferStartPosition = V & 0x1FF;
			    //fprintf(stderr, "%d\n", king->RAINBOWTransferStartPosition);
	                    //printf("RAINBOW Start Line: %d, %08x\n", fx_vce.raster_counter,  king->RAINBOWTransferStartPosition);
			   }
			   break;

		case 0x43: { uint32 next = king->RAINBOWTransferBlockCount;
                           REGSETHW(next, V, msh);
                           KING_RainbowWriteBlockCount((uint16)next);
                           }
                           //printf("KRAM Transfer Block Count: %d, %08x\n", fx_vce.raster_counter,  king->RAINBOWTransferBlockCount);
			   break;

		// Raster IRQ line
		case 0x44: if(!msh)
			   {
			    king->RasterIRQLine = V & 0x1FF;
                            //printf("Raster IRQ scanline: %d, %08x\n", fx_vce.raster_counter, king->RasterIRQLine);
			   }
			   break;

		case 0x50:
                           if(!msh)
                           {
                            /* Drain the decoder through the CPU write timestamp
                               under OLD transfer enables before altering R50. */
                            SoundBox_ADPCMUpdate(timestamp);
                            KING_WriteADPCMTransferControl(V);
                            RedoKINGIRQCheck();
                           }
                           break;

		case 0x51: REGSETHW(king->ADPCMBufferMode[0], V, msh);
			   RedoKINGIRQCheck();
			   break;

		case 0x52: REGSETHW(king->ADPCMBufferMode[1], V, msh);
			   RedoKINGIRQCheck();
			   break;

		case 0x58: REGSETHW(king->ADPCMSAL[0], V, msh); king->ADPCMSAL[0] &= 0x3FF; break;
		case 0x59: REGSETHW(king->ADPCMEndAddress[0], V, msh); king->ADPCMEndAddress[0] &= 0x3FFFF; break;
		case 0x5A: REGSETHW(king->ADPCMIntermediateAddress[0], V, msh); king->ADPCMIntermediateAddress[0] &= 0xFFF; break;

		case 0x5C: REGSETHW(king->ADPCMSAL[1], V, msh); king->ADPCMSAL[1] &= 0x3FF; break;
		case 0x5D: REGSETHW(king->ADPCMEndAddress[1], V, msh); king->ADPCMEndAddress[1] &= 0x3FFFF; break;
		case 0x5E: REGSETHW(king->ADPCMIntermediateAddress[1], V, msh); king->ADPCMIntermediateAddress[1] &= 0xFFF; break;

 
		case 0x61: if(king->KRAM_Mode != (V & 1))
			   {
			    KINGDBG("KRAM Mode Change To: %02x\n", V & 1);
			    king->KRAM_Mode = V & 0x1;
                RecalcKRAMPagePtrs();
			   }
			   break;
	      }

  PCFX_SetEvent(PCFX_EVENT_KING, timestamp + CalcNextExternalEvent(0x4FFFFFFF));	// TODO: Optimize this to only be called when necessary.
 }
}

/* HuC6272 R50/51/52/53: the address and end/intermediate events
 * advance once per requested ADPCM word; its *data* arrives only when the
 * word is granted on the K-BUS. These two operations are no longer conflated. */
static void KING_AdvanceADPCMAddress(int ch)
{
 king->ADPCMPlayAddress[ch] = (king->ADPCMPlayAddress[ch] & 0x20000) | ((king->ADPCMPlayAddress[ch] + 1) & 0x1FFFF);

 if(!(king->ADPCMPlayAddress[ch] & 0x1FFFF))
 {
  ADPCMDBG("Ch %d Wrapped", ch);
 }

 if(king->ADPCMPlayAddress[ch] == (((king->ADPCMEndAddress[ch] + 1) & 0x1FFFF) | (king->ADPCMEndAddress[ch] & 0x20000)) )
 {
  ADPCMDBG("Ch %d End", ch);

  if(!(king->ADPCMBufferMode[ch] & 1))
  {
   king->ADPCMControl &= ~(1 << ch);
   SoundBox_SetKINGADPCMControl(king->ADPCMControl);
  }
  else
  {
   king->ADPCMPlayAddress[ch] = king->ADPCMSAL[ch] << 8;
  }

  king->ADPCMStatus[ch] |= 1;

  if(king->ADPCMBufferMode[ch] & (0x1 << 1))
  {
   king->ADPCMIRQPending = TRUE;
   RedoKINGIRQCheck();
  }
 }
 else if(king->ADPCMPlayAddress[ch] == ((uint32)king->ADPCMIntermediateAddress[ch] << 6) )
 {
  ADPCMDBG("Ch %d Intermediate", ch);
  king->ADPCMStatus[ch] |= 2;

  if(king->ADPCMBufferMode[ch] & (0x2 << 1))
  {
   king->ADPCMIRQPending = TRUE;
   RedoKINGIRQCheck();
  }
 }


}

/* Compatibility/diagnostic synchronous accessor, not used by the v16 sound
 * transfer engine. The production -HSYNC request is asynchronous below. */
uint16 KING_GetADPCMHalfWord(int ch)
{
 const unsigned page=KING_KRAMPage((king->PageSetting & 0x0100)?1u:0u);
 const uint32 address=king->ADPCMPlayAddress[ch]&0x3FFFFu;
 const uint16 result=king->KRAM[page][address];
 KING_KBusReserve(page,KBUS_ADPCM,king_gfx_ts);
 KING_AdvanceADPCMAddress(ch);
 return result;
}

void KING_RequestADPCMHalfWord(int ch)
{
 if((unsigned)ch>=2u) return;
 const unsigned page=KING_KRAMPage((king->PageSetting & 0x0100)?1u:0u);
 const uint32 address=king->ADPCMPlayAddress[ch]&0x3FFFFu;
 if(king->ADPCMGrantPending[ch])
 {
  /* A new capture supersedes a still-unconsumed previous request; this
   * cannot normally occur at the documented 2+ HSYNC transfer period. */
  king->ADPCMGrantPending[ch]=0;
 }
 king->ADPCMGrantAddress[ch]=address;
 king->ADPCMGrantPage[ch]=(uint8)page;
 king->ADPCMGrantPending[ch]=1;
 const uint32 deadline=KING_KBusReserveTagged(page,KBUS_ADPCM,king_gfx_ts,
                                              KING_SCSI_PENDING_MAX+(unsigned)ch+1u);
 if(deadline==UINT32_MAX)
 {
  king->ADPCMGrantPending[ch]=0;
  fprintf(stderr,"KING K-BUS: ADPCM grant overflow at %u\n",king_gfx_ts);
 }
 KING_AdvanceADPCMAddress(ch);
}


bool KING_Init(void)
{
 memset(&fx_vce, 0, sizeof(fx_vce));
 if(!(king = (king_t*)calloc(1, sizeof(king_t))))
  return(0);

 king->lastts = 0;

 BGLayerDisable = 0;

 BuildCMT();

 // Build VCE priority map.
 // Don't change this unless you know what you're doing!
 // There may appear to be a bug in the pixel mixing
 // code elsewhere, because it accesses this array like [vdc][bg][rainbow], but it's not a bug.
 // This multi-dimensional array has no concept of bg, vdc, rainbow, or their orders per-se, it just
 // contains priority information for 3 different layers.

 for(uint_fast8_t bg_prio = 0; bg_prio < 8; bg_prio++)
  for(uint_fast8_t vdc_prio = 0; vdc_prio < 8; vdc_prio++)
   for(uint_fast8_t rainbow_prio = 0; rainbow_prio < 8; rainbow_prio++)
   {
    int bg_prio_test = bg_prio ? bg_prio : 0x10;
    int vdc_prio_test = vdc_prio ? vdc_prio : 0x10;
    int rainbow_prio_test = rainbow_prio ? rainbow_prio : 0x10;

    if(bg_prio_test >= 8)
     VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][0] = 3;
    else
    {
     if(bg_prio_test < vdc_prio_test && bg_prio_test < rainbow_prio_test)
      VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][0] = 0;
     else if(bg_prio_test > vdc_prio_test && bg_prio_test > rainbow_prio_test)
      VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][0] = 2;
     else 
      VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][0] = 1;
    }

    if(vdc_prio_test >= 8)
     VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][1] = 3;
    else
    {
     if(vdc_prio_test < bg_prio_test && vdc_prio_test < rainbow_prio_test)
      VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][1] = 0;
     else if(vdc_prio_test > bg_prio_test && vdc_prio_test > rainbow_prio_test)
      VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][1] = 2;
     else
      VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][1] = 1;
    }

    if(rainbow_prio_test >= 8)
     VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][2] = 3;
    else
    {
     if(rainbow_prio_test < bg_prio_test && rainbow_prio_test < vdc_prio_test)
      VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][2] = 0;
     else if(rainbow_prio_test > bg_prio_test && rainbow_prio_test > vdc_prio_test)
      VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][2] = 2;
     else
      VCEPrioMap[bg_prio][vdc_prio][rainbow_prio][2] = 1;
    }
   }

 SCSICD_Init(SCSICD_PCFX, 3, SoundBox_GetRawMixer(), 153600 * setting_cd_speed, 21477273, KING_CDIRQ, KING_StuffSubchannels);

 return(1);
}

void KING_Close(void)
{
 if(getenv("PCFX_RAINBOW_STATS") && strcmp(getenv("PCFX_RAINBOW_STATS"),"0"))
  fprintf(stderr,"RAINBOW read-ahead: requests=%llu granted=%llu consumed=%llu eager-words=%llu\n",
   (unsigned long long)rainbow_ahead_requests,(unsigned long long)rainbow_ahead_grants,
   (unsigned long long)rainbow_ahead_hits,(unsigned long long)rainbow_eager_words);
 if(getenv("PCFX_RAINBOW_STATS") && strcmp(getenv("PCFX_RAINBOW_STATS"),"0"))
  fprintf(stderr,"RAINBOW reads before pending prefetch grant=%llu (synchronous fallback)\n",
          (unsigned long long)rainbow_read_ahead_underflows);
 if(getenv("PCFX_RAINBOW_STATS") && strcmp(getenv("PCFX_RAINBOW_STATS"),"0") && king)
  fprintf(stderr,"RAINBOW async: granted-words=%u grant-retries=%u decoder-underflows=%u buffered-bytes=%u active=%u\n",
          king->RBAsyncWords,king->RBAsyncWaits,king->RBAsyncUnderflows,
          king->RBAsyncLength,king->RBAsyncActive);
 if(getenv("PCFX_RAINBOW_STATS") && strcmp(getenv("PCFX_RAINBOW_STATS"),"0") && king)
  fprintf(stderr,"RAINBOW Huffman tracker: complete=%u current_columns=%u stalls=%u bad=%u\n",
         king->RBHCompletedBlocks,king->RBH.completed_columns,king->RBH.stalls,king->RBH.invalid);
 if(getenv("PCFX_RAINBOW_STATS") && strcmp(getenv("PCFX_RAINBOW_STATS"),"0") && king)
  fprintf(stderr,"RAINBOW v23 MCU luma: columns=%u compare-blocks=%u exact-blocks=%u pixels=%u mismatches=%u\n",
          king->RBMCUColumns,king->RBMCUBlocks,king->RBMCUExactBlocks,king->RBMCUCompared,king->RBMCUDifferences);
 if(getenv("PCFX_RAINBOW_STATS") && strcmp(getenv("PCFX_RAINBOW_STATS"),"0"))
  fprintf(stderr,"RAINBOW incremental RLE: blocks=%u tokens=%u\n",
          king->RBAsyncRLEBlocks,king->RBAsyncRLETotalTokens);
 if(getenv("PCFX_RAINBOW_STATS") && strcmp(getenv("PCFX_RAINBOW_STATS"),"0") && king)
  fprintf(stderr,"RAINBOW grant-fed JPEG input: blocks=%u entropy-bytes=%u stuffed-pairs=%u split-pair-waits=%u\n",
          king->RBJPEGBlocks,king->RBJPEGEntropyBytes,
          king->RBJPEGStuffedPairs,king->RBJPEGStalledPairs);
 if(getenv("PCFX_SCSI_BACKPRESSURE_STATS") && strcmp(getenv("PCFX_SCSI_BACKPRESSURE_STATS"),"0"))
  fprintf(stderr,"SCSI K-BUS backpressure: rejected second-byte attempts=%llu\n",
          (unsigned long long)scsi_kbus_backpressure_retries);
 memset(&fx_vce, 0, sizeof(fx_vce));
 if(king)
 {
  free(king);
  king = NULL;
 }
 SCSICD_Close();
}


void KING_Reset(const v810_timestamp_t timestamp)
{
 KING_Update(timestamp);
 rainbow_ahead_hits=rainbow_ahead_grants=rainbow_ahead_requests=rainbow_eager_words=0;
 scsi_kbus_backpressure_retries=0;
 rainbow_read_ahead_underflows=0;

 VCE_ResetRegisters();

 int32 ltssave = king->lastts;
 memset(king, 0, sizeof(king_t));
 king->lastts = ltssave;

 king->Reg00 = 0;
 king->Reg01 = 0;
 king->Reg02 = 0;
 king->Reg03 = 0;
 king->dma_receive_active = FALSE;
 king->dma_send_active = FALSE;
 king->dma_cycle_counter = 0x7FFFFFFF;
 king->DMARetirePending = FALSE;
 king->PIOReadSeen = FALSE;
 king->PIOBusWedged = FALSE;
 king->BG0FetchRehomed = TRUE;
 king->RAINBOWConfigStateVersion = 1;
 king->ADPCMTransferLatchVersion = 1;
 king->KRAMWritePendingStateVersion = 1;
 king->SCSIPendingStateVersion = 1;
 king->ADPCMGrantStateVersion = 1;
 king->RainbowAheadStateVersion=1;
 king->RBAsyncStageVersion=1;
 king->RBAsyncRLEVersion=1;

 RecalcKRAMPagePtrs();

 HPhase = HPHASE_HBLANK_PART1;
 HPhaseCounter = 1;
 vdc_lb_pos = 0;

 memset(vdc_linebuffers, 0, sizeof(vdc_linebuffers));
 memset(vdc_linebuffer, 0, sizeof(vdc_linebuffer));
 memset(vdc_linebuffer_yuved, 0, sizeof(vdc_linebuffer_yuved));
 memset(rainbow_linebuffer, 0, sizeof(rainbow_linebuffer));
 memset(bg_linebuffer, 0, sizeof(bg_linebuffer));


 king->dma_cycle_counter = 0x7FFFFFFF;
 scsicd_ne = 1;	// FIXME

 RedoKINGIRQCheck();

 for(uint_fast16_t x = 0; x < 0x200; x++)
  RedoPaletteCache(x);

 DoHBlankVCECaching();

 SoundBox_SetKINGADPCMControl(0);

 SCSICD_Power(timestamp);

 memset(king->KRAM, 0xFF, sizeof(king->KRAM));
}


static inline void DRAWBG8x1_4(uint32 *target, const uint16 *cg, const uint32 *palette_ptr, const uint32 layer_or)
{
 if(*cg >> 14) target[0] = palette_ptr[(*cg >> 14)] | layer_or;
 if((*cg >> 12) & 0x3) target[1] = palette_ptr[((*cg >> 12) & 0x3)] | layer_or;
 if((*cg >> 10) & 0x3) target[2] = palette_ptr[((*cg >> 10) & 0x3)] | layer_or;
 if((*cg >> 8) & 0x3) target[3] = palette_ptr[((*cg >> 8) & 0x3)] | layer_or;
 if((*cg >> 6) & 0x3) target[4] = palette_ptr[((*cg >> 6) & 0x3)] | layer_or;
 if((*cg >> 4) & 0x3) target[5] = palette_ptr[((*cg >> 4) & 0x3)] | layer_or;
 if((*cg >> 2) & 0x3) target[6] = palette_ptr[((*cg >> 2) & 0x3)] | layer_or;
 if((*cg >> 0) & 0x3) target[7] = palette_ptr[((*cg >> 0) & 0x3)] | layer_or;
}

static inline void DRAWBG8x1_16(uint32 *target, const uint16 *cgptr, const uint32 *palette_ptr, const uint32 layer_or)
{
 if(cgptr[0] >> 12) target[0] = palette_ptr[((cgptr[0] >> 12))] | layer_or;
 if((cgptr[0] >> 8) & 0xF) target[1] = palette_ptr[(((cgptr[0] >> 8) & 0xF))] | layer_or;
 if((cgptr[0] >> 4) & 0xF) target[2] = palette_ptr[(((cgptr[0] >> 4) & 0xF))] | layer_or;
 if((cgptr[0] >> 0) & 0xF) target[3] = palette_ptr[(((cgptr[0] >> 0) & 0xF))] | layer_or;

 if(cgptr[1] >> 12) target[4] = palette_ptr[((cgptr[1] >> 12))] | layer_or;
 if((cgptr[1] >> 8) & 0xF) target[5] = palette_ptr[(((cgptr[1] >> 8) & 0xF))] | layer_or;
 if((cgptr[1] >> 4) & 0xF) target[6] = palette_ptr[(((cgptr[1] >> 4) & 0xF))] | layer_or;
 if((cgptr[1] >> 0) & 0xF) target[7] = palette_ptr[(((cgptr[1] >> 0) & 0xF))] | layer_or;
}

static inline void DRAWBG8x1_256(uint32 *target, const uint16 *cgptr, const uint32 *palette_ptr, const uint32 layer_or)
{
 if(cgptr[0] >> 8) target[0] = palette_ptr[(cgptr[0] >> 0x8)] | layer_or;
 if(cgptr[0] & 0xFF) target[1] = palette_ptr[(cgptr[0] & 0xFF)] | layer_or;
 if(cgptr[1] >> 8) target[2] = palette_ptr[(cgptr[1] >> 0x8)] | layer_or;
 if(cgptr[1] & 0xFF) target[3] = palette_ptr[(cgptr[1] & 0xFF)] | layer_or;
 if(cgptr[2] >> 8) target[4] = palette_ptr[(cgptr[2] >> 0x8)] | layer_or;
 if(cgptr[2] & 0xFF) target[5] = palette_ptr[(cgptr[2] & 0xFF)] | layer_or;
 if(cgptr[3] >> 8) target[6] = palette_ptr[(cgptr[3] >> 0x8)] | layer_or;
 if(cgptr[3] & 0xFF) target[7] = palette_ptr[(cgptr[3] & 0xFF)] | layer_or;
}

static inline void DRAWBG8x1_64K(uint32 *target, const uint16 *cgptr, const uint32 *palette_ptr, const uint32 layer_or)
{
 if(cgptr[0] & 0xFF00) target[0] = ((cgptr[0x0] & 0x00F0) << 8) | ((cgptr[0] & 0x000F)<<4) | ((cgptr[0] & 0xFF00) << 8) | layer_or;
 if(cgptr[1] & 0xFF00) target[1] = ((cgptr[0x1] & 0x00F0) << 8) | ((cgptr[1] & 0x000F)<<4) | ((cgptr[1] & 0xFF00) << 8) | layer_or;
 if(cgptr[2] & 0xFF00) target[2] = ((cgptr[0x2] & 0x00F0) << 8) | ((cgptr[2] & 0x000F)<<4) | ((cgptr[2] & 0xFF00) << 8) | layer_or;
 if(cgptr[3] & 0xFF00) target[3] = ((cgptr[0x3] & 0x00F0) << 8) | ((cgptr[3] & 0x000F)<<4) | ((cgptr[3] & 0xFF00) << 8) | layer_or;
 if(cgptr[4] & 0xFF00) target[4] = ((cgptr[0x4] & 0x00F0) << 8) | ((cgptr[4] & 0x000F)<<4) | ((cgptr[4] & 0xFF00) << 8) | layer_or;
 if(cgptr[5] & 0xFF00) target[5] = ((cgptr[0x5] & 0x00F0) << 8) | ((cgptr[5] & 0x000F)<<4) | ((cgptr[5] & 0xFF00) << 8) | layer_or;
 if(cgptr[6] & 0xFF00) target[6] = ((cgptr[0x6] & 0x00F0) << 8) | ((cgptr[6] & 0x000F)<<4) | ((cgptr[6] & 0xFF00) << 8) | layer_or;
 if(cgptr[7] & 0xFF00) target[7] = ((cgptr[0x7] & 0x00F0) << 8) | ((cgptr[7] & 0x000F)<<4) | ((cgptr[7] & 0xFF00) << 8) | layer_or;
}

static inline void DRAWBG8x1_16M(uint32 *target, const uint16 *cgptr, const uint32 *palette_ptr, const uint32 layer_or)
{
 if(cgptr[0] >> 8) target[0] = ((cgptr[0x0] & 0xFF00) << 8) | (cgptr[1] & 0xFF00) | (cgptr[1] & 0xFF) | layer_or;
 if(cgptr[0] & 0xFF) target[1] = ((cgptr[0x0] & 0x00FF) << 16) | (cgptr[1] & 0xFF00) | (cgptr[1] & 0xFF) | layer_or;
 if(cgptr[2] >> 8) target[2] = ((cgptr[0x2] & 0xFF00) << 8) | (cgptr[3] & 0xFF00) | (cgptr[3] & 0xFF) | layer_or;
 if(cgptr[2] & 0xFF) target[3] = ((cgptr[0x2] & 0x00FF) << 16) | (cgptr[3] & 0xFF00) | (cgptr[3] & 0xFF) | layer_or;
 if(cgptr[4] >> 8) target[4] = ((cgptr[0x4] & 0xFF00) << 8) | (cgptr[5] & 0xFF00) | (cgptr[5] & 0xFF) | layer_or;
 if(cgptr[4] & 0xFF) target[5] = ((cgptr[0x4] & 0x00FF) << 16) | (cgptr[5] & 0xFF00) | (cgptr[5] & 0xFF) | layer_or;
 if(cgptr[6] >> 8) target[6] = ((cgptr[0x6] & 0xFF00) << 8) | (cgptr[7] & 0xFF00) | (cgptr[7] & 0xFF) | layer_or;
 if(cgptr[6] & 0xFF) target[7] = ((cgptr[0x6] & 0x00FF) << 16) | (cgptr[7] & 0xFF00) | (cgptr[7] & 0xFF) | layer_or;
}

//static bool bgmode_warning = 0; // Debug

#include "king-bgfast.inc"

static inline int32 max(int32 a, int32 b)
{
 if(a > b)
  return(a);

 return(b);
}

static void DrawBG(uint32 *target, int n)
{
 // TODO: Verify behavior when size is out of bounds on BG1-3.
 // With BG0 at least, it behaves as if the size is at its minimum, with caveats(TO BE INVESTIGATED).
 const uint32 bg_ss_table[2][0x10] = 
 {
  { 0x3, 0x3, 0x3, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0x3, 0x3, 0x3, 0x3, 0x3 },
  { 0x3, 0x3, 0x3, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3 },
 };

 const bool bg_ss_invalid_table[2][0x10] =
 {
  { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 },
  { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1 },
 };

#if 0
 const uint32 cg_per_mode[0x8] = 
 {
  0, // Invalid mode
  1, // 2-bit mode
  2, // 4-bit mode
  4, // 8-bit mode
  8, // 16-bit mode
  8, // 16-bit mode
  8, // 16-bit mode
  8, // 16-bit mode
 };
#endif
 const uint32 layer_or = (LAYER_BG0 + n) << 28;

 const uint32 palette_offset = ((vce_rendercache.palette_offset[1 + (n >> 1)] >> ((n & 1) ? 8 : 0)) << 1) & 0x1FF;
 const uint32 *palette_ptr = &vce_rendercache.palette_table_cache[palette_offset];
 const uint32 bat_and_cg_page = KING_KRAMPage((king->PageSetting & 0x0010) ? 1 : 0);

 const uint16 bgmode = (king->bgmode >> (n * 4)) & 0xF;
 const bool endless = (king->BGScrollMode >> n) & 0x1;
 const uint32 XScroll = king->BGXScroll[n];
 const uint32 YScroll = king->BGYScroll[n];

 const uint32 YOffset = (YScroll + (fx_vce.raster_counter - 22)) & 0xFFFF;

 const uint32 bat_offset = king->BGBATAddr[n] * 1024;
 const uint32 bat_sub_offset = n ? bat_offset : (king->BG0SubBATAddr * 1024);
 const uint16 *bat_base = &king->KRAM[bat_and_cg_page][bat_offset & 0x20000];
 const uint16 *bat_sub_base = &king->KRAM[bat_and_cg_page][bat_sub_offset & 0x20000];

 const uint32 cg_offset = king->BGCGAddr[n] * 1024;
 /* BG0 direct-bitmap display fetch pointer left un-homed by a bit-depth switch
  * (see BG0FetchRehomed): the pointer was not returned to the CG base, so it is
  * one visible screen of fetched words ahead of where the scanline formula says.
  * The stock microprogram fetches too few CG words per line to refill the whole
  * line, so the display alternates freshly-fetched cells (correct data) with
  * cells still holding the stale, un-homed fetch -- the orange "jailbar" pattern
  * (024_king_mprog_bench).  bg0_stale_off is that un-homed displacement in words;
  * 0 means the pointer is homed (normal, no artifact). */
 uint32 bg0_stale_off = 0;
 uint32 bg0_fresh_cells = 32;   /* how many of the 32 cells/line get fresh data */
 if(n == 0 && !king->BG0FetchRehomed){
  const uint16 m0 = king->bgmode & 0x7; uint32 wpl;
  switch(m0){ case 1: wpl=32; break; case 2: wpl=64; break; case 3: wpl=128; break; default: wpl=256; break; }
  bg0_stale_off = wpl * 240u;

  /* How many CG words the microprogram actually fetches for BG0 per 16-slot
   * rotation.  The rotation repeats ~16 times across a 256px line (one 16-slot
   * pass per 16 display pixels), so it refills ~16 cells per active CG slot; if
   * that is fewer than the 32 cells a 2bpp line needs, the shortfall is the set
   * of cells left holding the un-homed fetch (the orange jailbars).  A schedule
   * with >=2 BG0 CG slots keeps up and shows no artifact. */
  if(king->MPROGControl & 0x1){
   int kcg = 0, i;
   for(i = 0; i < 16; i++){
    const uint16 mpd = king->MPROGData[i];
    if(((mpd >> 6) & 0x3) == 0 && !(mpd & 0x100) && !(mpd & 0x010)) kcg++;
   }
   bg0_fresh_cells = (uint32)kcg * 16u;
   if(bg0_fresh_cells > 32u) bg0_fresh_cells = 32u;
   if(bg0_fresh_cells == 0u) bg0_fresh_cells = 32u; /* nothing fetched: don't model */
  }
 }
 const uint32 cg_sub_offset = n ? cg_offset : (king->BG0SubCGAddr * 1024);
 const uint16 *cg_base = &king->KRAM[bat_and_cg_page][cg_offset & 0x20000];
 const uint16 *cg_sub_base = &king->KRAM[bat_and_cg_page][cg_sub_offset & 0x20000];

 KP_BG(bat_and_cg_page, bat_offset, cg_offset, bgmode);

 const int bat_bitsize_mask = (n ? 0x3FF : 0x7FF) >> 3;

 const uint32 bat_width_shift = bg_ss_table[(bool)n][(king->BGSize[n] & 0xF0) >> 4];
 const bool bat_width_invalid = bg_ss_invalid_table[(bool)n][(king->BGSize[n] & 0xF0) >> 4];
 const uint32 bat_width = (1 << bat_width_shift) >> 3;

 const int32 bat_height_shift = bg_ss_table[(bool)n][king->BGSize[n] & 0x0F];
 //const bool bat_height_invalid = bg_ss_invalid_table[(bool)n][king->BGSize[n] & 0x0F];
 const int32 bat_height = (1 << bat_height_shift) >> 3;

 const bool bat_sub_width_invalid = n ? bat_width_invalid : bg_ss_invalid_table[(bool)n][(king->BGSize[n] & 0xF000) >> 12];
 const uint32 bat_sub_width_shift = n ? bat_width_shift : bg_ss_table[(bool)n][(king->BGSize[n] & 0xF000) >> 12];
 const uint32 bat_sub_width = (1 << bat_sub_width_shift) >> 3;
 const uint32 bat_sub_width_mask = bat_sub_width - 1;
 const uint32 bat_sub_width_test = endless ? (bat_bitsize_mask + 1) : max(bat_width, bat_sub_width);

 const int32 bat_sub_height_shift = n ? bat_height_shift : bg_ss_table[(bool)n][(king->BGSize[n] & 0x0F00) >> 8];
 const int32 bat_sub_height = (1 << bat_sub_height_shift) >> 3;
 const int32 bat_sub_height_mask = bat_sub_height - 1;
 const int32 bat_sub_height_test = endless ? (bat_bitsize_mask + 1) : max(bat_height, bat_sub_height);

 uint16 cg_mask[8];
 uint16 cg_remap[8];
 bool cg_ofbat[8];

 uint16 cg_sub_mask[8];
 uint16 cg_sub_remap[8];
 bool cg_sub_ofbat[8];

 bool BATFetchCycle = FALSE;
 bool BATSubFetchCycle = FALSE;

 const bool rotate_mode = (n == 0) && (king->priority & 0x1000);

 // If the bg mode is invalid, don't draw this layer, duuhhhh
 if(!(bgmode & 0x7))
  return;

 if((bgmode & 0x7) >= 6)
 {
  /*if(!bgmode_warning)
  {
   //printf("Unsupported KING BG Mode for KING BG %d: %02x\n", n, bgmode);
   bgmode_warning = TRUE;
  }*/
  return;
 }

 memset(cg_mask, 0, sizeof(cg_mask));
 memset(cg_remap, 0, sizeof(cg_remap));
 memset(cg_ofbat, 0, sizeof(cg_ofbat));
 memset(cg_sub_mask, 0, sizeof(cg_sub_mask));
 memset(cg_sub_remap, 0, sizeof(cg_sub_remap));
 memset(cg_sub_ofbat, 0, sizeof(cg_sub_ofbat));

 if(king->MPROGControl & 0x1)
 {
  int remap_thing = 0;
  int remap_sub_thing = 0;

  for(uint_fast8_t x = 0; x < 8; x++)
  {
   uint16 mpd;

   // Forcing CG and BAT to 0 if the affine bit != rotate_mode is not technically correct.
   // If there is a mismatch, it's more likely the effective CG and BAT address for the pixel/segment
   // being drawn won't be calculated correctly, likely being just the initial offsets.

   mpd = king->MPROGData[((cg_offset & 0x20000) ? 0x8 : 0x0) + x];
   if(((mpd >> 6) & 0x3) == n && !(mpd & 0x100) && !(mpd & 0x010))
   {
    cg_mask[remap_thing] = 0xFFFF;
    cg_remap[remap_thing] = mpd & 0x7;
    cg_ofbat[remap_thing] = mpd & 0x8;

    if((bool)(mpd & 0x20) != rotate_mode)
    {
     KINGDBG("Affine bit != rotate_mode?");
     cg_mask[remap_thing] = 0;
    }
    remap_thing++;
   }

   mpd = king->MPROGData[((cg_sub_offset & 0x20000) ? 0x8 : 0x0) + x];
   if(((mpd >> 6) & 0x3) == n && !(mpd & 0x100) && !(mpd & 0x010))
   {
    cg_sub_mask[remap_sub_thing] = 0xFFFF;
    cg_sub_remap[remap_sub_thing] = mpd & 0x7;
    cg_sub_ofbat[remap_sub_thing] = mpd & 0x8;

    if((bool)(mpd & 0x20) != rotate_mode)
    {
     KINGDBG("Affine bit != rotate_mode? (SUB)");
     cg_sub_mask[remap_sub_thing] = 0;
    }
    remap_sub_thing++;
   }
  }

  for(uint_fast8_t x = 0; x < 8; x++)
  {
   uint16 mpd;

   mpd = king->MPROGData[((bat_offset & 0x20000) ? 0x8 : 0x0) + x];
   if(((mpd >> 6) & 0x3) == n && !(mpd & 0x100) && (mpd & 0x010) && (bool)(mpd & 0x020) == rotate_mode)
    BATFetchCycle = TRUE;

   mpd = king->MPROGData[((bat_sub_offset & 0x20000) ? 0x8 : 0x0) + x];
   if(((mpd >> 6) & 0x3) == n && !(mpd & 0x100) && (mpd & 0x010) && (bool)(mpd & 0x020) == rotate_mode)
    BATSubFetchCycle = TRUE;
  }
 }

 int bat_y = (YOffset >> 3) & bat_bitsize_mask;
 uint32 bat_x = (XScroll >> 3) & bat_bitsize_mask;
 int ysmall = YOffset & 0x7;

 const uint32 bat_invalid_y_mask = bat_width_invalid ? 0 : 0xFFFFFFFF;
 const uint32 bat_invalid_sub_y_mask = bat_sub_width_invalid ? 0 : 0xFFFFFFFF;

 if(rotate_mode)
  target += 8;
 else
  target += 8 - (XScroll & 0x7);


 {
  int32 wmul = (1 << bat_width_shift), wmask = ((1 << bat_height_shift) - 1) & bat_invalid_y_mask;
  int32 sexy_y_pos = (YOffset & wmask) * wmul;
  
  int32 wmul_sub = (1 << bat_sub_width_shift), wmask_sub = ((1 << bat_sub_height_shift) - 1) & bat_invalid_sub_y_mask;
  int32 sexy_y_sub_pos = (YOffset & wmask_sub) * wmul_sub;


  #define ROTCODE_PRE	\
	 const int32 bat_width_mask = endless ? (bat_width - 1) : 0xFFFF;		\
	 const int32 bat_height_mask = endless ? (bat_height - 1) : 0xFFFF;		\
         int32 a, b, c, d;	\
         int32 raw_x_coord = (int32)sign_11_to_s16(XScroll) - (int16)king->BGAffinCenterX;	\
         int32 raw_y_coord = fx_vce.raster_counter + (int32)sign_11_to_s16(YScroll) - 22 - (int16)king->BGAffinCenterY;		\
         int32 xaccum;	\
         int32 yaccum;	\
	\
         a = (int16)king->BGAffinA;	\
         b = (int16)king->BGAffinB;	\
         c = (int16)king->BGAffinC;	\
         d = (int16)king->BGAffinD;	\
	\
         xaccum = raw_x_coord * a + raw_y_coord * b;	\
         yaccum = raw_y_coord * d + raw_x_coord * c;	\
         xaccum += (int16)king->BGAffinCenterX << 8;	\
         yaccum += (int16)king->BGAffinCenterY << 8;	\

  #define ROTCODE_LOOP_PRE	\
         for(uint_fast16_t x = 0; x < 256; x++)	\
         {	\
          uint16 new_x = (xaccum >> 8);	\
          uint16 new_y = (yaccum >> 8);	\
          const uint16 *cgptr;	\
          bat_x = (new_x >> 3) & bat_width_mask;	\
          bat_y = (new_y >> 3) & bat_height_mask;	\
          ysmall = new_y & 0x7;	

  #define ROTCODE_LOOP_POST	\
          xaccum += a;	\
          yaccum += c;	\
         }

  if((bgmode & 0x7) == BGMODE_4 && rotate_mode)
  {
         ROTCODE_PRE;

         ROTCODE_LOOP_PRE;
	 uint32 pbn = 0;
         if(bgmode & 0x8)
         {
          uint16 bat = bat_base[(bat_offset + (bat_x + ((bat_y << bat_width_shift) >> 3)  )) & 0x1FFFF];
          pbn = ((bat >> 12) << 2);
          bat &= 0x0FFF;
          cgptr = &cg_base[(cg_offset + (bat * 8) + ysmall) & 0x1FFFF];
         }
         else
         {
          cgptr = &cg_base[(cg_offset + bat_x + ((new_y & wmask) * wmul / 8)) & 0x1FFFF];
         }
         uint8 ze_cg = (cgptr[0] >> ((7 - (new_x & 7)) << 1)) & 0x03;

         if(endless || (bat_x < bat_width && bat_y < bat_height))
         {
          if(ze_cg) target[x] = palette_ptr[pbn + ze_cg] | layer_or;
         }
        ROTCODE_LOOP_POST;
  }
  else if((bgmode & 0x7) == BGMODE_16 && rotate_mode)
  {
         ROTCODE_PRE;

         ROTCODE_LOOP_PRE;
	 uint32 pbn = 0;

         if(bgmode & 0x8)
         {
          uint16 bat = bat_base[(bat_offset + (bat_x + ((bat_y << bat_width_shift) >> 3)  )) & 0x1FFFF];
          pbn = ((bat >> 12) << 4);
	  bat &= 0x0FFF;
          cgptr = &cg_base[(cg_offset + (bat * 16) + ysmall * 2) & 0x1FFFF];
         }
         else
         {
          cgptr = &cg_base[(cg_offset + (bat_x * 2) + ((new_y & wmask) * wmul / 4)) & 0x1FFFF];
         }
         uint8 ze_cg = (cgptr[(new_x >> 2) & 0x1] >> ((3 - (new_x & 3)) << 2)) & 0x0F;

         if(endless || (bat_x < bat_width && bat_y < bat_height))
         {
          if(ze_cg) target[x] = palette_ptr[pbn + ze_cg] | layer_or;
         }
        ROTCODE_LOOP_POST;
  }
  else if((bgmode & 0x7) == BGMODE_256 && rotate_mode)
  {
	 ROTCODE_PRE;

	 ROTCODE_LOOP_PRE;
          if(bgmode & 0x8)
          {
           uint16 bat = bat_base[(bat_offset + (bat_x + ((bat_y << bat_width_shift) >> 3)  )) & 0x1FFFF];
           cgptr = &cg_base[(cg_offset + (bat * 32) + ysmall * 4) & 0x1FFFF];
          }
          else
          {
           cgptr = &cg_base[(cg_offset + (bat_x * 4) + ((new_y & wmask) * wmul / 2)) & 0x1FFFF];
          }
          uint8 ze_cg = cgptr[(new_x >> 1) & 0x3] >> (((new_x & 1) ^ 1) << 3);

          if(endless || (bat_x < bat_width && bat_y < bat_height))
          {
           if(ze_cg) target[x] = palette_ptr[ze_cg] | layer_or;
          }
	 ROTCODE_LOOP_POST;
  } 
  else if((bgmode & 0x7) == BGMODE_64K && rotate_mode)
  {
	ROTCODE_PRE;
	ROTCODE_LOOP_PRE;
          if(bgmode & 0x8)
          {
           uint16 bat = bat_base[(bat_offset + (bat_x + ((bat_y << bat_width_shift) >> 3)  )) & 0x1FFFF];
           cgptr = &cg_base[(cg_offset + (bat * 64) + ysmall * 8) & 0x1FFFF];
          }
          else
          {
           cgptr = &cg_base[(cg_offset + (bat_x * 8) + ((new_y & wmask) * wmul)) & 0x1FFFF];
          }
          uint16 ze_cg = cgptr[new_x & 0x7];

          if(endless || (bat_x < bat_width && bat_y < bat_height))
          {
           if(ze_cg >> 8) target[x] = ((ze_cg & 0x00F0) << 8) | ((ze_cg & 0x000F)<<4) | ((ze_cg & 0xFF00) << 8) | layer_or;
          }
         ROTCODE_LOOP_POST;
  }
  else switch(bgmode & 0x7)
  {
#define DRAWBG8x1_MAC(cg_needed, blit_suffix, pbn_arg)	\
			 for(uint_fast16_t x = 0; x < 256 + 8; x+= 8)     	\
                         {                                      	\
                          if(bat_x < bat_width && bat_y < bat_height)	\
                          {                                     	\
                           uint32 eff_bat_loc = bat_offset;     	\
                           uint16 bat = 0;                      	\
                           uint16 pbn MDFN_NOWARN_UNUSED = 0;           \
  		  	   const uint16 *cgptr[2];			\
			   uint16 cg[cg_needed];			\
									\
                           if(bgmode & 0x8)                     	\
                            eff_bat_loc += bat_x + (((bat_y & bat_invalid_y_mask) << bat_width_shift) >> 3);       	\
                                                                	\
                           eff_bat_loc &= 0x1FFFF;		  	\
                                                                	\
                           if(BATFetchCycle)                    	\
                            bat = bat_base[eff_bat_loc];		\
									\
			   if(bgmode & 0x08)				\
			    pbn = bat >> 12;				\
			   bat &= 0xFFF;				\
									\
                           cgptr[0] = &cg_base[(cg_offset + (bat_x * cg_needed) + sexy_y_pos)	\
				& 0x1FFFF];      						\
		           cgptr[1] = &cg_base[(cg_offset + (bat * 8 * cg_needed) + ysmall * cg_needed) \
				& 0x1FFFF];							\
													\
		           for(int cow = 0; cow < cg_needed; cow++)					\
		            cg[cow] = cgptr[cg_ofbat[cow]][cg_remap[cow]] & cg_mask[cow];		\
													\
		           DRAWBG8x1_##blit_suffix(target + x, cg, palette_ptr + pbn_arg, layer_or);	\
			  }                                     					\
			  else if(bat_x < bat_sub_width_test && bat_y < bat_sub_height_test)		\
			  {										\
                           uint32 eff_bat_loc = bat_sub_offset;         \
                           uint16 bat = 0;                              \
                           uint16 pbn MDFN_NOWARN_UNUSED = 0;           \
                           const uint16 *cgptr[2];                      \
                           uint16 cg[cg_needed];                        \
                                                                        \
                           if(bgmode & 0x8)                             \
                            eff_bat_loc += (bat_x & bat_sub_width_mask) + (((bat_y & bat_invalid_sub_y_mask & bat_sub_height_mask) << bat_sub_width_shift) >> 3);           \
                                                                        \
                           eff_bat_loc &= 0x1FFFF;          \
                                                                        \
                           if(BATSubFetchCycle)                         \
                            bat = bat_sub_base[eff_bat_loc];            \
                                                                        \
                           if(bgmode & 0x08)                            \
                            pbn = bat >> 12;                            \
                           bat &= 0xFFF;                                \
                                                                        \
                           cgptr[0] = &cg_sub_base[(cg_sub_offset + ((bat_x & bat_sub_width_mask) * cg_needed) + sexy_y_sub_pos)   \
                                & 0x1FFFF];                                                 \
                           cgptr[1] = &cg_sub_base[(cg_sub_offset + (bat * 8 * cg_needed) + ysmall * cg_needed) \
                                & 0x1FFFF];                                                 \
                                                                                                        \
                           for(int cow = 0; cow < cg_needed; cow++)                                     \
                            cg[cow] = cgptr[cg_sub_ofbat[cow]][cg_sub_remap[cow]] & cg_sub_mask[cow];   \
                                                                                                        \
                           DRAWBG8x1_##blit_suffix(target + x, cg, palette_ptr + pbn_arg, layer_or);    \
			  }										\
                          bat_x = (bat_x + 1) & bat_bitsize_mask; 					\
                         }


   case 0x01: // 4 color, 1/4 byte per pixel :b
        sexy_y_pos >>= 3;
	sexy_y_sub_pos >>= 3;

	if(bg0_stale_off && bg0_fresh_cells < 32u && !(bgmode & 0x8))
	{
	 /* Un-homed BG0 direct bitmap whose microprogram under-fetches: only
	  * bg0_fresh_cells of the 32 cells get a fresh, correctly-addressed word;
	  * the rest still hold the un-homed fetch pointer's stale word.  The fresh
	  * cells are spread evenly across the line, so the display shows the real
	  * text in those columns and the stale (orange) data in the gaps -- the
	  * "jailbar" pattern.  Fresh cells read the scanline address; stale cells
	  * read it plus the un-homed displacement. */
	 uint32 bx = bat_x, cell = 0;
	 for(uint_fast16_t x = 0; x < 256 + 8; x += 8, cell++)
	 {
	  if(bx < bat_width)
	  {
	   const bool fresh = ((cell * bg0_fresh_cells) & 31u) < bg0_fresh_cells;
	   const uint32 off = fresh ? 0u : bg0_stale_off;
	   const uint16 *cgptr = &cg_base[(cg_offset + off + (bx * 1) + sexy_y_pos) & 0x1FFFF];
	   DRAWBG8x1_4(target + x, cgptr, palette_ptr, layer_or);
	  }
	  bx = (bx + 1) & bat_bitsize_mask;
	 }
	}
	else
	 DRAWBG8x1_MAC(1, 4, (pbn << 2));
	break;

   case 0x02: // 16 color, 1/2 byte per pixel
	sexy_y_pos >>= 2;
	sexy_y_sub_pos >>= 2;

	DRAWBG8x1_MAC(2, 16, (pbn << 4));
        break;

   case 0x03: // 256 color, 1 byte per pixel palettized - OK
	sexy_y_pos >>= 1;
	sexy_y_sub_pos >>= 1;

	DRAWBG8x1_MAC(4, 256, 0);
	break;

   case 0x04: // 64K color, 2 bytes per pixel - OK
	DRAWBG8x1_MAC(8, 64K, 0);
	break;

   case 0x05: // 16M color, 2 bytes per pixel - OK
	DRAWBG8x1_MAC(8, 16M, 0);
	break;
  #if 0
   case BGMODE_64K_EXTDOT:
	break;

   case BGMODE_16M_EXTDOT:
	{
	 uint32 fat_bat = ((YOffset & ((1 << bat_height_shift) - 1)) << bat_width_shift) + (((XScroll & ~ 7) & ((1 << bat_width_shift) - 1));
	}
        break;
  #endif
  }
 }
}

static int16 UVLUT[65536][3];
static uint8 RGBDeflower[1152]; // 0 is at 384
static uint32 CbCrLUT[65536];

static void RebuildUVLUT(void)
{
 /* One color reconstruction for all TETSU output: KING, RAINBOW, palettes. */
 for(uint_fast16_t ur = 0; ur < 256; ur++)
 {
  for(uint_fast16_t vr = 0; vr < 256; vr++)
  {
   int r, g, b;
   /* All TETSU output planes share the HuC6261 analog YUV DAC. */
   const PCFX_YUVColorOffsets offset = PCFX_AnalogYUVOffsets(ur, vr);
   r = offset.r;
   g = offset.g;
   b = offset.b;

   UVLUT[vr + ur * 256][0] = r;
   UVLUT[vr + ur * 256][1] = g;
   UVLUT[vr + ur * 256][2] = b;
 
   CbCrLUT[vr + ur * 256] = clamp_to_u8(128 + ((r * -9699 + g * -19071 + b * 28770) >> 16)) << CBSHIFT;
   CbCrLUT[vr + ur * 256] |= clamp_to_u8(128 + ((r * 28770 + g * -24117 + b * -4653) >> 16)) << CRSHIFT;

   //printf("%d %d %d, %08x\n", r, g, b, CbCrLUT[vr + ur * 256]);
  }
 }
 for(uint_fast16_t x = 0; x < 1152; x++)
 {
  if(x < 384) RGBDeflower[x] = 0;
  else if(x > (384 + 255)) RGBDeflower[x] = 255;
  else
   RGBDeflower[x] = x - 384;
 }
}

// FIXME
static int rs, gs, bs;
static uint32 inline YUV888_TO_RGB888(uint32 yuv)
{
 const uint8 y = yuv >> 16;
 uint8 r, g, b;

 r = clamp_to_u8((int32)(y + UVLUT[yuv & 0xFFFF][0]));
 g = clamp_to_u8((int32)(y + UVLUT[yuv & 0xFFFF][1]));
 b = clamp_to_u8((int32)(y + UVLUT[yuv & 0xFFFF][2]));

 return MAKECOLOR(r, g, b, 0xFF);
}

#ifdef HAVE_HUC6273
MDFN_Pixel FXVCE_GetPaletteNative(uint16 index)
{
 const uint32 yuv12 = fx_vce.palette_table[index & 0x1FF];
 const uint32 yuv888 = PCFX_YUVPalettePacked(yuv12);
 return (MDFN_Pixel)YUV888_TO_RGB888(yuv888);
}
#endif

static uint32 inline YUV888_TO_YCbCr888(uint32 yuv)
{
 uint32 y;

 y = 16 + ((((yuv >> 16) & 0xFF) * 220) >> 8);

 return(y | CbCrLUT[yuv & 0xFFFF]);
}

// FIXME: 
//static unsigned int lines_per_frame; //= (fx_vce.picture_mode & 0x1) ? 262 : 263;
#ifndef PCFX_HIGH_DOTCLOCK_WIDTH
#define PCFX_HIGH_DOTCLOCK_WIDTH 0
#endif
static uint32 HighDotClockWidth = PCFX_HIGH_DOTCLOCK_WIDTH ? PCFX_HIGH_DOTCLOCK_WIDTH : 1024;
static VDC **vdc_chips;

static inline uint32 ClampOutputWidth(uint32 width)
{
 if(width < 1)
  width = 256;
 if(width > internal_pitch)
  width = internal_pitch;
 if(width > 1024)
  width = 1024;
 return width;
}

static inline uint32 GetHighDotClockVDCWidth(void)
{
 uint32 width = 0;
 if(vdc_chips)
 {
  for(uint_fast8_t chip = 0; chip < 2; chip++)
   if(vdc_chips[chip])
   {
    const uint32 chip_width = VDC_GetCachedDisplayWidth(vdc_chips[chip]);
    if(chip_width > width)
     width = chip_width;
   }
 }
 return ClampOutputWidth(width ? width : 341);
}

static inline uint32 GetHighDotClockOutputWidth(void)
{
 if(HighDotClockWidth)
  return ClampOutputWidth(HighDotClockWidth);
 return GetHighDotClockVDCWidth();
}

static inline void SharpBilinearMap256ToWidth(uint32 out_x, uint32 out_width, uint32 *src0, uint32 *src1, uint32 *weight)
{
 if(out_width <= 256)
 {
  *src0 = *src1 = out_x & 0xFF;
  *weight = 0;
  return;
 }

 // Center-aligned 256 -> out_width coordinate transform.  Fixed point is 16.16.
 // src = ((out_x + 0.5) * 256 / out_width) - 0.5
 int64 src_fp = (((int64)(out_x * 2 + 1) * 256) << 15) / (int64)out_width - 0x8000;

 if(src_fp <= 0)
 {
  *src0 = *src1 = 0;
  *weight = 0;
  return;
 }

 const int64 max_fp = ((int64)255 << 16);
 if(src_fp >= max_fp)
 {
  *src0 = *src1 = 255;
  *weight = 0;
  return;
 }

 *src0 = (uint32)(src_fp >> 16);
 *src1 = *src0 + 1;
 uint32 frac = (uint32)(src_fp & 0xFFFF);

 // Sharp bilinear: bilinear only through a narrowed transition region,
 // equivalent to the common shader form clamp((frac - 0.5) * scale + 0.5).
 // This avoids nearest-neighbor column dropping without the full-width blur of
 // ordinary linear scaling.
 int64 sharp = ((int64)frac - 0x8000) * (int64)out_width / 256 + 0x8000;
 if(sharp < 0) sharp = 0;
 if(sharp > 0x10000) sharp = 0x10000;
 *weight = (uint32)sharp;
}

static inline uint32 BlendSameLayerYUV(uint32 a, uint32 b, uint32 weight)
{
 const uint32 layer = a & 0xF0000000;

 if(!weight || a == b)
  return a;
 if(weight >= 0x10000)
  return b;

 const int32 ay = (a >> 16) & 0xFF;
 const int32 au = (a >>  8) & 0xFF;
 const int32 av = (a >>  0) & 0xFF;
 const int32 by = (b >> 16) & 0xFF;
 const int32 bu = (b >>  8) & 0xFF;
 const int32 bv = (b >>  0) & 0xFF;

 const uint32 y = (uint32)(ay + (((by - ay) * (int32)weight + 0x8000) >> 16));
 const uint32 u = (uint32)(au + (((bu - au) * (int32)weight + 0x8000) >> 16));
 const uint32 v = (uint32)(av + (((bv - av) * (int32)weight + 0x8000) >> 16));

 return layer | (y << 16) | (u << 8) | v;
}

static inline uint32 SampleKING256Sharp(const uint32 *src, uint32 out_x, uint32 out_width)
{
 uint32 src0, src1, weight;
 SharpBilinearMap256ToWidth(out_x, out_width, &src0, &src1, &weight);

 const uint32 a = src[src0];
 const uint32 b = src[src1];

 if(a == b || src0 == src1)
  return a;

 // Do not blur through transparency or across KING/Rainbow layer boundaries.
 // Priority and transparency stay crisp; only same-layer color ramps are
 // horizontally reconstructed.
 if((a & 0xF0000000) && ((a ^ b) & 0xF0000000) == 0)
  return BlendSameLayerYUV(a, b, weight);

 return (weight < 0x8000) ? a : b;
}
static MDFN_Rect *DisplayRect;
static int32 *LineWidths;
static int skip;

void KING_StartFrame(VDC **arg_vdc_chips, EmulateSpecStruct *espec)	//MDFN_Surface *arg_surface, MDFN_Rect *arg_DisplayRect, MDFN_Rect *arg_LineWidths, int arg_skip)
{
 vdc_chips = arg_vdc_chips;
 DisplayRect = &espec->DisplayRect;
 LineWidths = espec->LineWidths;
 skip = espec->skip;

 //MDFN_DispMessage("P0:%06x P1:%06x; I0: %06x I1: %06x", king->ADPCMPlayAddress[0], king->ADPCMPlayAddress[1], king->ADPCMIntermediateAddress[0] << 6, king->ADPCMIntermediateAddress[1] << 6);
 //MDFN_DispMessage("%d %d\n", SCSICD_GetACK(), SCSICD_GetREQ());

 // For the case of interlaced mode(clear ~0 state)
 LineWidths[0] = 0;
#ifdef PCFX_HEADLESS
 /* Change the headless crop at frame entry, not after the field is drawn.
    In 480i, the two 240-line scanouts weave into separate parity rows. */
 pcfx_headless_video_set_interlaced((fx_vce.active_picture_mode & 0x2u) != 0);
#endif

 // These 2 should be overwritten in the big loop below.
 DisplayRect->x = 0;
 DisplayRect->w = 256;

 DisplayRect->y = setting_initial_scanline;
 DisplayRect->h = setting_last_scanline - DisplayRect->y + 1;
}

static int rb_type;
//  unsigned int width = (fx_vce.picture_mode & 0x08) ? 341 : 256;

/* KING R10 BG mode takes effect immediately (C6272 §2.3.5).
   Build a complete candidate line in the current register state, but only
   replace the remaining dots after a mid-line write. This accounts for the
   visible raster split; sub-dot KRAM fetch arbitration is not modeled. */
static void RenderKINGBackgroundLine(uint32 *dest)
{
   MDFN_FastU32MemsetM8(dest + 8, 0, 256);

    // Only bother to draw the BGs if the microprogram is enabled.
   if(king->MPROGControl & 0x1)
   {
    for(uint_fast8_t prio = 1; prio <= 7; prio++)
    {
     for(uint_fast8_t x = 0; x < 4; x++)
     {
      int thisprio = (king->priority >> (x * 3)) & 0x7;

      if(BGLayerDisable & (1 << x)) continue;

      if(thisprio == prio)
      {
       //if(fx_vce.raster_counter == 50)
        // CanDrawBG_Fast(x);

       // TODO/FIXME: TEST MORE
       if(CanDrawBG_Fast(x)) // && (rand() & 1))
	DrawBG_Fast(dest, x);
       else
        DrawBG(dest, x);
      }
     }
    }
   }

}

static void KING_RefreshBGModeAtCurrentDot(void)
{
 if(skip || HPhase != HPHASE_ACTIVE || fx_vce.in_hblank ||
    fx_vce.raster_counter < 22 || fx_vce.raster_counter >= 262)
  return;
 const unsigned elapsed = HPhaseCounter >= 1024 ? 0u : 1024u - (unsigned)HPhaseCounter;
 const unsigned first_dot = elapsed / 4u;
 if(first_dot >= 256) return;
 uint32 updated[256 + 8 + 8] = {0};
 RenderKINGBackgroundLine(updated);
 memcpy(bg_linebuffer + 8 + first_dot, updated + 8 + first_dot,
        (256u - first_dot) * sizeof(uint32));
}

static void DrawActive(void)
{
 rb_type = -1;

 if(fx_vce.raster_counter == king->RAINBOWTransferStartPosition && (king->RAINBOWTransferControl & 1))
 {
  king->RAINBOWStartPending = TRUE;

  //printf("Rainbow start pending: line=%d, busycount=%d, blockcount=%d\n", fx_vce.raster_counter, king->RAINBOWBusyCount, king->RAINBOWBlockCount);

  //if(fx_vce.raster_counter == 262)
  // puts("MOOO");
 }

 if(fx_vce.raster_counter < 262)
 {
  if(king->RAINBOWBusyCount)
  {
   /* Keep the read buffer pending until its grant-fed compressed input is
    * complete; no fabricated zero bytes from an empty KRAM FIFO. */
   if(king->RAINBOWBusyCount>1 || !king->RBAsyncActive)
    king->RAINBOWBusyCount--;
   if(!king->RAINBOWBusyCount)
   {
    RAINBOW_SwapBuffers();
    /* Now the old 16-raster block is finished. Activate any deferred
       REG 41/43 settings before considering the next block. */
    KING_RainbowCommitPending();
   }
  }

  if(!king->RAINBOWBusyCount)
  {
   bool WantDecode = FALSE;
   bool FirstDecode = FALSE;

   if(!king->RAINBOWBlockCount && king->RAINBOWStartPending)
   {
    //printf("Rainbow start real: %d %d\n", fx_vce.raster_counter, king->RAINBOWTransferBlockCount);
    king->RAINBOWBlockCount = king->RAINBOWActiveBlockCount;
    if(king->RAINBOWBlockCount)
    {
     KING_RainbowAheadDiscard();
     KING_RainbowAsyncCancel();
     king->RAINBOWKRAMReadPos = king->RAINBOWActiveKRAMA << 1;
     king->RBAsyncProducerPos=king->RAINBOWKRAMReadPos;
     FirstDecode = TRUE;
    }
   }

   if(king->RAINBOWBlockCount)
   {
    king->RAINBOWBlockCount--;
    WantDecode = TRUE;
   }

   if(WantDecode)
   {
    king->RAINBOWBusyCount = 16;

    if(fx_vce.raster_counter == 262)
     king->RAINBOWBusyCount++;

    // If we ever change the emulation time range from the current 0 through 262/263, we will need to readjust this
    // statement to prevent the previous frame's skip value to mess up the current frame's graphics data, since
    // RAINBOW data is delayed by 16 scanlines from when it's decoded(16 + 15 maximum delay).
    KING_RainbowAsyncStart(FirstDecode, skip && fx_vce.raster_counter < 246,
                           king_gfx_ts);
   }
  }

  rb_type = RAINBOW_FetchRaster(skip ? NULL : rainbow_linebuffer, LAYER_RAINBOW << 28, &vce_rendercache.palette_table_cache[((vce_rendercache.palette_offset[3] >> 0) & 0xFF) << 1]);

  king->RAINBOWStartPending = FALSE;
 } // end   if(fx_vce.raster_counter < 262)

 if(fx_vce.raster_counter >= 22 && fx_vce.raster_counter < 262)
 {
  if(!skip)
  {
   if(rb_type == 1) // YUV
   {
    // Only chroma key when we're not in 7.16MHz pixel mode
    if(!(fx_vce.active_picture_mode & 0x08))
    {
     const unsigned int ymin = vce_rendercache.ChromaKeyY & 0xFF;
     const unsigned int ymax = vce_rendercache.ChromaKeyY >> 8;
     const unsigned int umin = vce_rendercache.ChromaKeyU & 0xFF;
     const unsigned int umax = vce_rendercache.ChromaKeyU >> 8;
     const unsigned int vmin = vce_rendercache.ChromaKeyV & 0xFF;
     const unsigned int vmax = vce_rendercache.ChromaKeyV >> 8;

     if((vce_rendercache.ChromaKeyY | vce_rendercache.ChromaKeyU | vce_rendercache.ChromaKeyV) == 0)
     {
      //puts("Opt: 0 chroma key");
      for(uint_fast16_t x = 0; x < 256; x++)
      {
       if(!(rainbow_linebuffer[x] & 0xFFFFFF))
        rainbow_linebuffer[x] = 0;
      }
     }
     else if(ymin == ymax && umin == umax && vmin == vmax)
     {
      const uint32 compare_color = (ymin << 16) | (umin << 8) | (vmin << 0);

      //puts("Opt: Single color chroma key");

      for(uint_fast16_t x = 0; x < 256; x++)
      {
       if((rainbow_linebuffer[x] & 0xFFFFFF) == compare_color)
        rainbow_linebuffer[x] = 0;
      }
     }
     else if(ymin <= ymax && umin <= umax && vmin <= vmax)
     {
      const uint32 yv_min_sub = (ymin << 16) | vmin;
      const uint32 u_min_sub = umin << 8;
      const uint32 yv_max_add = ((0xFF - ymax) << 16) | (0xFF - vmax);
      const uint32 u_max_add = (0xFF - umax) << 8;

      for(uint_fast16_t x = 0; x < 256; x++)
      {
       const uint32 pixel = rainbow_linebuffer[x];
       const uint32 yv = pixel & 0xFF00FF;
       const uint32 u = pixel & 0x00FF00;
       uint32 testie;

       testie = ((yv - yv_min_sub) | (yv + yv_max_add)) & 0xFF00FF00;
       testie |= ((u - u_min_sub) | (u + u_max_add)) & 0x00FF00FF;

       if(!testie)
        rainbow_linebuffer[x] = 0;
      }
     }
     /*else
     {
      //puts("Opt: color keying off\n");
     }*/
    }
   }

    /*
        4 = Foremost
        1 = Hindmost
        0 = Hidden
    */

   RenderKINGBackgroundLine(bg_linebuffer);

  } // end if(!skip)
 } // end if(fx_vce.raster_counter >= 22 && fx_vce.raster_counter < 262)
}

static inline void VDC_PIXELMIX(bool SPRCOMBO_ON, bool BGCOMBO_ON)
{
    static const uint32 vdc_layer_num[2] = { LAYER_VDC_BG << 28, LAYER_VDC_SPR << 28};
    const uint32 vdc_poffset[2] = {
                                (((uint32)vce_rendercache.palette_offset[0] >> 0) & 0xFF) << 1, // BG
                                (((uint32)vce_rendercache.palette_offset[0] >> 8) & 0xFF) << 1 // SPR
                               };

    const uint_fast16_t width = fx_vce.dot_clock ? GetHighDotClockVDCWidth() : 256;

    for(uint_fast16_t x = 0; x < width; x++)
    {
     uint32 zort[2] = { vdc_linebuffers[0][x], vdc_linebuffers[1][x] };
     uint32 tmp_pixel;

     /* Keep disabled PC-FX VDC BG/SPR layers out of the VDC-A/VDC-B pre-mixer.
        This mirrors the SGX overscan-priority fix's important side effect: a
        non-visible VDC pixel must not participate in the mixed pixel decision. */
     for(unsigned int chip = 0; chip < 2; chip++)
     {
      const uint32 layer = (zort[chip] & 0x100) ? LAYER_VDC_SPR : LAYER_VDC_BG;
      if((zort[chip] & 0xF) && !vce_rendercache.LayerPriority[layer])
       zort[chip] = 0;
     }
   
     /* SPR combination */
     if(SPRCOMBO_ON && (zort[1] & 0x18F) > 0x180)
      tmp_pixel = (zort[1] & 0xF) | ((zort[0] & 0xF) << 4) | 0x100;
     /* BG combination  */                                                      
     else if(BGCOMBO_ON && ((zort[1] ^ 0x100) & 0x18F) > 0x180)
      tmp_pixel = (zort[1] & 0xF) | ((zort[0] & 0xF) << 4);
     else
      tmp_pixel = (zort[1] & 0xF) ? zort[1] : zort[0];

     vdc_linebuffer[x] = tmp_pixel;
     vdc_linebuffer_yuved[x] = 0;
     if(tmp_pixel & 0xF)
      vdc_linebuffer_yuved[x] = vce_rendercache.palette_table_cache[(tmp_pixel & 0xFF) + vdc_poffset[(tmp_pixel >> 8) & 1]] | vdc_layer_num[(tmp_pixel >> 8) & 1];
    }
}

static void MixVDC(void) NO_INLINE;
static void MixVDC(void)
{
    // Optimization for when both layers are disabled in the VCE.
    if(!vce_rendercache.LayerPriority[LAYER_VDC_BG] && !vce_rendercache.LayerPriority[LAYER_VDC_SPR])
    {
     MDFN_FastU32MemsetM8(vdc_linebuffer_yuved, 0, 512);
    }
    else switch(vce_rendercache.picture_mode & 0xC0)
    {
     case 0x00: VDC_PIXELMIX(0, 0); break;      // None on
     case 0x40: VDC_PIXELMIX(0, 1); break;      // BG combo on
     case 0x80: VDC_PIXELMIX(1, 0); break;      // SPR combo on
     case 0xC0: VDC_PIXELMIX(1, 1); break;      // Both on
    }
}


/* C6261 §2.6.3: alternate scanlines offset horizontally by half a dot.
   In the 7MHz clock domain that is 1.5/3 mixer clocks; in the 5MHz
   domain it is 0.5/1 output dot. The output is discrete RGB, so half
   samples are represented by filtered samples (not an analog sync waveform).
   Only the second field's rows shift, keeping the first field as an anchor.
   Rightmost half-dot is masked, as described in the manual.
   This is deliberately downstream of compositing and upstream of frontend
   downsampling, so the 1024-clock mixed waveform is preserved up to here. */
static void VCE_ApplyHalfDotShift(MDFN_Pixel *line, unsigned width, bool high_clock)
{
 if(width < 2) return;
 /* 7MHz: +1.5 mixer samples; 5MHz: +0.5 pixel sample.
    Sample the original line at x - shift, moving content to the right. */
 const unsigned whole = high_clock ? 1 : 0;
 const MDFN_Pixel left = line[0];
 MDFN_Pixel scratch[1024];
 for(unsigned x = 0; x < width; x++)
 {
   MDFN_Pixel a = x > whole ? line[x - whole - 1] : left;
   MDFN_Pixel b = x >= whole ? line[x - whole] : left;
   /* RGB channel-wise average; MAKECOLOR respects active output format. */
#if defined(MDFN_PIXEL_FORMAT_RGB565)
   unsigned ar = ((a >> 11)&31), ag = ((a >> 5)&63), ab=(a&31);
   unsigned br = ((b >> 11)&31), bg = ((b >> 5)&63), bb=(b&31);
   MDFN_Pixel shifted = ((ar+br+1)/2<<11) | ((ag+bg+1)/2<<5) | ((ab+bb+1)/2);
#elif defined(MDFN_PIXEL_FORMAT_RGB555)
   unsigned ar = ((a >> 10)&31), ag = ((a >> 5)&31), ab=(a&31);
   unsigned br = ((b >> 10)&31), bg = ((b >> 5)&31), bb=(b&31);
   MDFN_Pixel shifted = ((ar+br+1)/2<<10) | ((ag+bg+1)/2<<5) | ((ab+bb+1)/2);
#else
   MDFN_Pixel shifted = MAKECOLOR((((a >> RED_SHIFT)&255)+((b >> RED_SHIFT)&255)+1)/2,
                                  (((a >> GREEN_SHIFT)&255)+((b >> GREEN_SHIFT)&255)+1)/2,
                                  (((a >> BLUE_SHIFT)&255)+((b >> BLUE_SHIFT)&255)+1)/2, 255);
#endif
   scratch[x] = shifted;
 }
 /* Hardware masks half of the rightmost nominal 5MHz dot. In the 7MHz
    1024-tick representation this is one complete tick and half another.
    The digital approximation integrates the half-covered end sample. */
 const unsigned edge = high_clock ? width-2 : width-1;
 MDFN_Pixel p = scratch[edge];
#if defined(MDFN_PIXEL_FORMAT_RGB565)
 scratch[edge] = (p & 0xF7DEu) >> 1;
#elif defined(MDFN_PIXEL_FORMAT_RGB555)
 scratch[edge] = (p & 0x7BDEu) >> 1;
#else
 scratch[edge] = MAKECOLOR(((p>>RED_SHIFT)&255)/2,
                           ((p>>GREEN_SHIFT)&255)/2,
                           ((p>>BLUE_SHIFT)&255)/2, 255);
#endif
 if(high_clock) scratch[width-1] = MAKECOLOR(0, 0, 0, 255);
 memcpy(line, scratch, width * sizeof(*line));
}

/* Packed C6261 YUV (8:4:4) to 8:8:8 at the digital mixer input. */
static inline uint32 VCE_ExpandYUV16(uint16 packed)
{
 return ((uint32)(packed & 0xFF00u) << 8) |
        ((uint32)(packed & 0x00F0u) << 8) |
        ((uint32)(packed & 0x000Fu) << 4);
}

static void MixLayers(void)
{
	//uint32 *pXBuf = surface->pixels;
	MDFN_Pixel *pXBuf = internal_pix;

    // Now we have to mix everything together... I'm scared, mommy.
    // We have, vdc_linebuffer[0] and bg_linebuffer
    // Which layer is specified in bits 28-31(check the enum earlier on)
    uint32 priority_remap[8];
    uint32 ble_cache[8];
    bool ble_cache_any = FALSE;

    for(uint_fast8_t n = 0; n < 8; n++)
    {
     priority_remap[n] = vce_rendercache.LayerPriority[n];
     //printf("%d: %d\n", n, priority_remap[n]);
    }

    // Rainbow layer disabled?
    if(rb_type == -1 || RAINBOWLayerDisable)
     priority_remap[LAYER_RAINBOW] = 0;

    ble_cache[LAYER_NONE] = 0;
    for(uint_fast8_t x = 0; x < 4; x++)
     ble_cache[LAYER_BG0 + x] = (vce_rendercache.BLE >> (4 + x * 2)) & 0x3;

    ble_cache[LAYER_VDC_BG] = (vce_rendercache.BLE >> 0) & 0x3;
    ble_cache[LAYER_VDC_SPR] = (vce_rendercache.BLE >> 2) & 0x3;
    ble_cache[LAYER_RAINBOW] = (vce_rendercache.BLE >> 12) & 0x3;

    for(uint_fast8_t x = 0; x < 8; x++)
     if(ble_cache[x])
     {
      ble_cache_any = TRUE;
      break;
     }
   
    uint8 *coeff_cache_y_back[3];
    int8 *coeff_cache_u_back[3], *coeff_cache_v_back[3];
    uint8 *coeff_cache_y_fore[3];
    int8 *coeff_cache_u_fore[3], *coeff_cache_v_fore[3];

    for(uint_fast8_t x = 0; x < 3; x++)
    {
     coeff_cache_y_fore[x] = vce_rendercache.coefficient_mul_table_y[(vce_rendercache.coefficients[x * 2 + 0] >> 8) & 0xF];
     coeff_cache_u_fore[x] = vce_rendercache.coefficient_mul_table_uv[(vce_rendercache.coefficients[x * 2 + 0] >> 4) & 0xF];
     coeff_cache_v_fore[x] = vce_rendercache.coefficient_mul_table_uv[(vce_rendercache.coefficients[x * 2 + 0] >> 0) & 0xF];

     coeff_cache_y_back[x] = vce_rendercache.coefficient_mul_table_y[(vce_rendercache.coefficients[x * 2 + 1] >> 8) & 0xF];
     coeff_cache_u_back[x] = vce_rendercache.coefficient_mul_table_uv[(vce_rendercache.coefficients[x * 2 + 1] >> 4) & 0xF];
     coeff_cache_v_back[x] = vce_rendercache.coefficient_mul_table_uv[(vce_rendercache.coefficients[x * 2 + 1] >> 0) & 0xF];
    }

    MDFN_Pixel *target;
    uint32 BPC_Cache = (LAYER_NONE << 28); // Backmost pixel color(cache)
#ifdef PCFX_HEADLESS
        /* C6261 section 2.6: odd field first, even field vertically offset.
           Weave *different* decoded field lines, never copy/scale the prior field. */
        if(fx_vce.active_picture_mode & 0x2u)
          target = pXBuf + internal_pitch * (2u * (fx_vce.raster_counter - 22u) + (fx_vce.odd_field ? 0u : 1u));
        else
#endif
          target = pXBuf + internal_pitch * (fx_vce.raster_counter - 22u);
    

    // If at least one layer is enabled with the HuC6261, hindmost color is palette[0]
    // If no layers are on, this color is black.
    // If front cellophane is enabled, this color is forced to black(TODO:  Confirm on a real system.  Black or from CCR).
    // If back cellophane is enabled, this color is forced to the value in CCR
    //  (back and front conditions are handled closer to the pixel mixing loops down below)
    // TODO:  Test on a real PC-FX to see if CCR is used or not if back cellophane is enabled even if all layers are disabled in the HuC6261, 
    //  or if it just outputs black.
    // TODO:  See if enabling front/back cellophane in high dot-clock mode will set the hindmost color, even though the cellophane color mixing
    //  is disabled in high dot-clock mode.
    if(vce_rendercache.picture_mode & 0x7F00)
     BPC_Cache |= vce_rendercache.palette_table_cache[0];
    else			
     BPC_Cache |= 0x008080;

#define DOCELLO(pixpoo) \
	if((pixel[pixpoo] >> 28) != LAYER_VDC_SPR || ((vce_rendercache.SPBL >> ((vdc_linebuffer[x] & 0xF0)>> 4)) & 1))	\
        {	\
         int which_co = (ble_cache[pixel[pixpoo] >> 28] - 1);	\
         uint8 back_y = coeff_cache_y_back[which_co][(zeout >> 16) & 0xFF];	\
         int8 back_u = coeff_cache_u_back[which_co][(zeout >>  8) & 0xFF];	\
         int8 back_v = coeff_cache_v_back[which_co][(zeout >>  0) & 0xFF];	\
         uint8 fore_y = coeff_cache_y_fore[which_co][(pixel[pixpoo] >> 16) & 0xFF];	\
         int8 fore_u = coeff_cache_u_fore[which_co][(pixel[pixpoo] >>  8) & 0xFF];	\
         int8 fore_v = coeff_cache_v_fore[which_co][(pixel[pixpoo] >>  0) & 0xFF];	\
         zeout = (pixel[pixpoo] & 0xFF000000) | ((RGBDeflower + 384)[back_y + fore_y] << 16) | ((RGBDeflower + 384)[back_u + fore_u + 128] << 8) | ((RGBDeflower + 384)[back_v + fore_v + 128] << 0);	\
        } else zeout = pixel[pixpoo];	\

#define DOCELLOSPECIALFRONT() \
        {       \
         uint8 y = coeff_cache_y_back[0][(zeout >> 16) & 0xFF];    \
         int8 u = coeff_cache_u_back[0][(zeout >>  8) & 0xFF];    \
         int8 v = coeff_cache_v_back[0][(zeout >>  0) & 0xFF];    \
         zeout = (zeout & 0xFF000000) | ((RGBDeflower + 384)[CCR_Y_front + y] << 16) | ((RGBDeflower + 384)[CCR_U_front + u + 128] << 8) | 	\
			((RGBDeflower + 384)[CCR_V_front + v + 128] << 0); \
        }

#define	LAYER_MIX_BODY_PIX(bg_pix, rb_pix, index_vdc) \
      { uint32 pixel[4];	\
      uint32 prio[3];	\
      uint32 zeout = BPC_Cache;	\
      const uint32 _vdc_pix = vdc_linebuffer_yuved[index_vdc]; \
      const uint32 _bg_pix = (bg_pix); \
      const uint32 _rb_pix = (rb_pix); \
      prio[0] = priority_remap[_vdc_pix >> 28];  \
      prio[1] = priority_remap[_bg_pix >> 28];	\
      prio[2] = priority_remap[_rb_pix >> 28];	\
      pixel[0] = 0;	\
      pixel[1] = 0;	\
      pixel[2] = 0;	\
      {	\
       uint8 pi0 = VCEPrioMap[prio[0]][prio[1]][prio[2]][0];	\
       uint8 pi1 = VCEPrioMap[prio[0]][prio[1]][prio[2]][1];	\
       uint8 pi2 = VCEPrioMap[prio[0]][prio[1]][prio[2]][2];	\
       /*assert(pi0 == 3 || !pixel[pi0]);*/ pixel[pi0] = _vdc_pix; 	\
       /*assert(pi1 == 3 || !pixel[pi1]);*/ pixel[pi1] = _bg_pix;	\
       /*assert(pi2 == 3 || !pixel[pi2]);*/ pixel[pi2] = _rb_pix;		\
      }

#define	LAYER_MIX_BODY(index_256, index_vdc) \
      LAYER_MIX_BODY_PIX((bg_linebuffer + 8)[index_256], rainbow_linebuffer[index_256], index_vdc)

#ifdef HAVE_HUC6273
#define HUC6273_STORE_VCE_TOP_PRIO() huc6273_vce_top_prio_line[x] = (uint8)priority_remap[(zeout >> 28) & 0x7]
#else
#define HUC6273_STORE_VCE_TOP_PRIO() do { } while(0)
#endif

#define LAYER_MIX_FINAL_NOCELLO	\
       if(pixel[0])	\
        zeout = pixel[0];	\
       if(pixel[1])	\
         zeout = pixel[1];	\
       if(pixel[2])	\
         zeout = pixel[2];	\
       HUC6273_STORE_VCE_TOP_PRIO(); \
       target[x] = YUV888_TO_xxx(zeout);	\
      }

// For back cellophane, the hindmost pixel is always a valid pixel to mix with, a "layer" in its own right,
// so we don't need to check the current pixel value before mixing.
#define LAYER_MIX_FINAL_BACK_CELLO     \
      if(pixel[0])        \
      { \
       if(ble_cache[pixel[0] >> 28])     \
       {        \
        DOCELLO(0);     \
       }        \
       else     \
        zeout = pixel[0];       \
      } \
      if(pixel[1])        \
      { \
       if(ble_cache[pixel[1] >> 28])     \
       {        \
        DOCELLO(1);     \
       }        \
       else     \
        zeout = pixel[1];       \
      } \
      if(pixel[2])        \
      { \
       if(ble_cache[pixel[2] >> 28])     \
       {        \
        DOCELLO(2);     \
       }        \
       else     \
        zeout = pixel[2];       \
      } \
      HUC6273_STORE_VCE_TOP_PRIO(); \
       target[x] = YUV888_TO_xxx(zeout);      \
     }

// ..however, for front and "normal" cellophane, we need to make sure that the
// layer is indeed a real layer(KBG, VDC, RAINBOW) before mixing.
// Note: We need to check the upper 4 bits in determining whether the previous pixel is from a real layer or not, because the default
// hindmost non-layer color in front cellophane and normal cellophane modes is black, and black is represented in YUV as non-zero.  We COULD bias/XOR each of U/V
// by 0x80 in the rendering code so that it would work if we just tested for the non-zeroness of the previous pixel, and adjust the YUV->RGB
// to compensate...TODO as a future possible optimization(MAYBE, it would obfuscate things more than they already are).
//
// Also, since the hindmost real layer pixel will never mix with anything behind it, we can leave
// out a few checks for the first possible hindmost real pixel.
//
// Also, the front cellophane effect itself doesn't need to check if the effective pixel output is a real layer (TODO: Confirm on real hardware!)
//
#define LAYER_MIX_FINAL_FRONT_CELLO	\
      if(pixel[0])	\
       zeout = pixel[0];	\
      if(pixel[1])	\
      {	\
       if(ble_cache[pixel[1] >> 28] && (zeout & (0xF << 28))) \
       {	\
        DOCELLO(1);	\
       }	\
       else	\
        zeout = pixel[1];	\
      }	\
      if(pixel[2])	\
      {	\
       if(ble_cache[pixel[2] >> 28] && (zeout & (0xF << 28)))	\
       {	\
        DOCELLO(2);	\
       }	\
       else	\
        zeout = pixel[2];	\
      }	\
      if(!pixel[0] && !pixel[1] && !pixel[2]) \
       zeout = VCE_ExpandYUV16(vce_rendercache.CCR); \
      else \
       DOCELLOSPECIALFRONT(); \
      HUC6273_STORE_VCE_TOP_PRIO(); \
       target[x] = YUV888_TO_xxx(zeout);	\
     }

#define LAYER_MIX_FINAL_CELLO	\
      if(pixel[0])	\
       zeout = pixel[0];	\
      if(pixel[1])	\
      {	\
       if(ble_cache[pixel[1] >> 28] && (zeout & (0xF << 28))) 	\
       {	\
        DOCELLO(1);	\
       }	\
       else	\
        zeout = pixel[1];	\
      }	\
      if(pixel[2])	\
      {	\
       if(ble_cache[pixel[2] >> 28] && (zeout & (0xF << 28)))	\
       {	\
        DOCELLO(2);	\
       }	\
       else	\
        zeout = pixel[2];	\
      }	\
      HUC6273_STORE_VCE_TOP_PRIO(); \
       target[x] = YUV888_TO_xxx(zeout);	\
     }


	#ifdef YUV_YCBCR
	#define YUV888_TO_xxx YUV888_TO_YCbCr888
	#else
	#define YUV888_TO_xxx YUV888_TO_RGB888
	#endif
	#include "king_mix_body.inc"
	#undef YUV888_TO_xxx

    DisplayRect->w = fx_vce.dot_clock ? GetHighDotClockOutputWidth() : 256;
    DisplayRect->x = 0;

#ifdef HAVE_HUC6273
    if(PCFX_HuC6273Active())
    {
     uint8 aurora_prio = 0;
     if(priority_remap[LAYER_VDC_BG] > aurora_prio) aurora_prio = (uint8)priority_remap[LAYER_VDC_BG];
     if(priority_remap[LAYER_VDC_SPR] > aurora_prio) aurora_prio = (uint8)priority_remap[LAYER_VDC_SPR];
     if(priority_remap[LAYER_RAINBOW] > aurora_prio) aurora_prio = (uint8)priority_remap[LAYER_RAINBOW];
     HuC6273_RenderLinePriority(target, fx_vce.raster_counter - 22, DisplayRect->w, huc6273_vce_top_prio_line, aurora_prio);
    }
#endif

    /* Apply documented mode-3 displacement AFTER HuC6273 priority and
       TETSU colour compositing, so any replacement VDC layer shifts too. */
    if((vce_rendercache.picture_mode & 3u) == 3u && !fx_vce.odd_field)
     VCE_ApplyHalfDotShift(target, DisplayRect->w, fx_vce.dot_clock);

	// FIXME
	LineWidths[fx_vce.raster_counter - 22] = DisplayRect->w;
}

static inline void RunVDCs(const int master_cycles, uint16 *pixels0, uint16 *pixels1)
{
 int32 div_clocks;

 fx_vce.clock_divider += master_cycles;
 div_clocks = fx_vce.clock_divider / fx_vce.dot_clock_ratio;
 fx_vce.clock_divider -= div_clocks * fx_vce.dot_clock_ratio;

 if(pixels0)
  pixels0 += vdc_lb_pos;

 if(pixels1)
  pixels1 += vdc_lb_pos;

 //assert((vdc_lb_pos + div_clocks) <= 512);

 fx_vce.vdc_event[0] = VDC_Run(vdc_chips[0], div_clocks, pixels0, pixels0 ? false : true);
 fx_vce.vdc_event[1] = VDC_Run(vdc_chips[1], div_clocks, pixels1, pixels1 ? false : true);

 vdc_lb_pos += div_clocks;

// printf("%d\n", vdc_lb_pos);
// if(fx_vce.dot_clock)
//  assert(vdc_lb_pos <= 342);
// else
//  assert(vdc_lb_pos <= 257);
}

static void MDFN_FASTCALL KING_RunGfx(int32 clocks)
{
 while(clocks > 0)
 {
  int32 chunk_clocks = clocks;

  if(chunk_clocks > HPhaseCounter)
   chunk_clocks = HPhaseCounter;

  clocks -= chunk_clocks;
  HPhaseCounter -= chunk_clocks;
  king_gfx_ts += chunk_clocks;

  if(skip)
   RunVDCs(chunk_clocks, NULL, NULL);
  else if(fx_vce.in_hblank)
  {
   static uint16 dummybuf[1024];
   RunVDCs(chunk_clocks, dummybuf, dummybuf);
  }
  else
  {
   RunVDCs(chunk_clocks, vdc_linebuffers[0], vdc_linebuffers[1]);
  }

  //assert(HPhaseCounter >= 0);

  while(HPhaseCounter <= 0)
  {
   HPhase = (HPhase + 1) % HPHASE_COUNT;
   switch(HPhase)
   {
    case HPHASE_ACTIVE: vdc_lb_pos = 0;
			fx_vce.in_hblank = false;
			DoHBlankVCECaching();
			DrawActive();
			HPhaseCounter += 1024;
			break;

    case HPHASE_HBLANK_PART1:
                        if(!skip)
                        {
                         if(fx_vce.raster_counter >= 22 && fx_vce.raster_counter < 262)
                         {
#ifdef PCFX_HEADLESS
                          if(fx_vce.raster_counter == 22 && (fx_vce.active_picture_mode & 2u))
                           pcfx_headless_video_note_field(fx_vce.odd_field);
#endif
                          MixVDC();
                          MixLayers();
                         }
                        }
			fx_vce.in_hblank = true;
                        /* §3.2.3: five refreshes per HBlank. Both banks are
                         * reserved together; placing them at HBlank entry is
                         * a modelling assumption, not a documented phase. */
                        for(unsigned refresh=0;refresh<5;refresh++)
                         for(unsigned bank=0;bank<2;bank++)
                          KING_KBusReserve(bank, KBUS_REFRESH, king_gfx_ts);
                        fx_vce.in_vdc_hsync = true;
                        SoundBox_HSync(king_gfx_ts);
                        /* Pending R50 enables must not retroactively decode the
                           elapsed pre-HSYNC interval with their new value. */
                        if(king->ADPCMPendingEnable)
                         SoundBox_ADPCMUpdate(king_gfx_ts);
                        KING_ADPCMEnableAtHSync();
                        /* HuC6230's two-byte capture is synchronized to the
                           same falling -HSYNC, after new R50 enables latch. */
                        SoundBox_ADPCMTransferHSync();

                        for(uint_fast8_t chip = 0; chip < 2; chip++)
                         VDC_HSync(vdc_chips[chip], true);

			HPhaseCounter += 48;
			break;

    case HPHASE_HBLANK_PART3:
                        /* HuC6261 C6261 §2.6: 525 scanlines over two NTSC
                           interlace fields, with odd/even status on SR bit 14.
                           Mode 2 alternates 263/262 scanlines.  Mode 3 also
                           adds half-dot shift on alternate scanlines. */
                        const unsigned mode = fx_vce.active_picture_mode & 3u;
                        const unsigned lines = (mode & 2u)
                          ? (fx_vce.odd_field ? 263u : 262u)
                          : ((mode & 1u) ? 262u : 263u);
                        fx_vce.raster_counter = (fx_vce.raster_counter + 1u) % lines;
                        if(fx_vce.raster_counter == 0)
                        {
                          VCE_LatchVerticalControl();
                          /* The manual specifies O/E=odd for the FIRST field
                             after entering interlace, then odd/even alternation. */
                          if(!(mode & 2u) && (fx_vce.active_picture_mode & 2u))
                           fx_vce.odd_field = true;
                          else if(mode & 2u)
                           fx_vce.odd_field = !fx_vce.odd_field;
                          else
                           fx_vce.odd_field = false;
                        }

                        if(fx_vce.raster_counter == 0)
                        {
#ifdef HAVE_HUC6273
                         HuC6273_SetTime(king_gfx_ts);
                         HuC6273_FrameBoundary();
#endif
                         for(uint_fast8_t chip = 0; chip < 2; chip++)
                          VDC_VSync(vdc_chips[chip], true);
                        }

                        if(fx_vce.raster_counter == 3)
                         for(uint_fast8_t chip = 0; chip < 2; chip++)
                          VDC_VSync(vdc_chips[chip], false);

			if(!fx_vce.raster_counter)
			{
			 V810_Exit();
			}

			if(fx_vce.raster_counter == king->RasterIRQLine && (king->RAINBOWTransferControl & 0x2))
			{
			 //printf("Wovely: %d, %d, %d\n", fx_vce.raster_counter, king->RAINBOWRasterCounter, king->RAINBOWTransferControl);

			 //if(fx_vce.raster_counter == 262)
			 //{
			 // printf("Rainbow raster IRQ on line 262?\n");
			 //}
			 //else
			 {
			  king->RasterIRQPending = TRUE;
			  RedoKINGIRQCheck();
			 }
			}

            // Preserve total line timing: 173+120 is constant in high clock.
            // The adjustable VDC HSYNC edge is an uncalibrated alignment parameter.

			if(fx_vce.dot_clock)
			 HPhaseCounter += 173 - vdc_hsync_phase_ticks;
			else
			 HPhaseCounter += 165;
			break;

    case HPHASE_HBLANK_PART4:
			fx_vce.in_vdc_hsync = false;
                        for(int chip = 0; chip < 2; chip++)
			 VDC_HSync(vdc_chips[chip], false);

			if(fx_vce.dot_clock)
			 HPhaseCounter += 120 + vdc_hsync_phase_ticks;
			else
 		  	 HPhaseCounter += 128;
			break;

   } // end: switch(HPhase)
  } // end: while(HPhaseCounter <= 0)
 } // end: while(clocks > 0)
} // end KING_RunGfx()

void KING_SetPixelFormat(void) 
{
 rs = RSHIFT_MEDNAFEN;
 gs = GSHIFT_MEDNAFEN;
 bs = BSHIFT_MEDNAFEN;
 RebuildUVLUT();
}

void KING_SetLayerEnableMask(uint64 mask)
{
	uint64 ms = mask;
	// "BG0\0BG1\0BG2\0BG3\0VDC-A BG\0VDC-A SPR\0VDC-B BG\0VDC-B SPR\0RAINBOW\0",

	BGLayerDisable = (~ms) & 0xF;
	ms >>= 4;

	for(uint_fast8_t chip = 0; chip < 2; chip++)
	{
		VDC_SetLayerEnableMask(fx_vdc_chips[chip], ms & 0x3);
		ms >>= 2;
	}

	RAINBOWLayerDisable = (~ms) & 0x1;
	
	// Assigned to a value that is never used
	//ms >>= 1;
}


int KING_StateAction(StateMem *sm, int load, int data_only)
{
 SFORMAT KINGStateRegs[] =
 {
  SFVARN(king->AR, "AR"),
  SFARRAY16N(king->KRAM[0], 262144, "KRAM0"),
  SFARRAY16N(king->KRAM[1], 262144, "KRAM1"),
  SFVARN(king->KRAMWA, "KRAMWA"),
  SFVARN(king->KRAMRA, "KRAMRA"),
  SFVARN(king->KRAM_Mode, "KRAM_Mode"),
  SFVARN(king->PageSetting, "PageSetting"),
  SFVARN(king->bgmode, "bgmode"),
  SFVARN(king->priority, "priority"),
  SFVARN(king->BGScrollMode, "BGScrollMode"),
  SFARRAY16N(king->BGSize, 4, "BGSize"),

  SFARRAYN(king->BGBATAddr, 4, "BGBATAddr"),
  SFARRAYN(king->BGCGAddr, 4, "BGCGAddr"),
  SFVARN(king->BG0FetchRehomed, "BG0FetchRehomed"),
  SFVARN(king->BG0SubBATAddr, "BG0SubBATAddr"),
  SFVARN(king->BG0SubCGAddr, "BG0SubCGAddr"),

  SFARRAY16N(king->BGXScroll, 4, "BGXScroll"),
  SFARRAY16N(king->BGYScroll, 4, "BGYScroll"),
  SFVARN(king->BGAffinA, "BGAffinA"),
  SFVARN(king->BGAffinB, "BGAffinB"),
  SFVARN(king->BGAffinC, "BGAffinC"),
  SFVARN(king->BGAffinD, "BGAffinD"),
  SFVARN(king->BGAffinCenterX, "BGAffinCenterX"),
  SFVARN(king->BGAffinCenterY, "BGAffinCenterY"),

  SFVARN(king->ADPCMControl, "ADPCMControl"),
  SFVARN(king->ADPCMControlWritten, "ADPCMControlWritten"),
  SFVARN(king->ADPCMPendingEnable, "ADPCMPendingEnable"),
  SFVARN(king->ADPCMTransferLatchVersion, "ADPCMTransferLatchVersion"),
  SFARRAY16N(king->ADPCMBufferMode, 2, "ADPCMBufferMode"),
  SFARRAY16N(king->ADPCMSAL, 2, "ADPCMSAL"),
  SFARRAY32N(king->ADPCMEndAddress, 2, "ADPCMEndAddress"),
  SFARRAY32N(king->ADPCMPlayAddress, 2, "ADPCMPlayAddress"),
  SFARRAY16N(king->ADPCMIntermediateAddress, 2, "ADPCMIntermediateAddress"),
  SFARRAY16N(king->ADPCMStatus, 2, "ADPCMStatus"),
  SFVARN(king->ADPCMIRQPending, "ADPCMIRQPending"),
  SFVARN(king->RAINBOWTransferControl, "RAINBOWTransferControl"),
  SFVARN(king->RAINBOWKRAMA, "RAINBOWKRAMA"),
  SFVARN(king->RAINBOWTransferStartPosition, "RAINBOWTransferStartPosition"),
  SFVARN(king->RAINBOWTransferBlockCount, "RAINBOWTransferBlockCount"),
  SFVARN(king->RAINBOWActiveKRAMA, "RAINBOWActiveKRAMA"),
  SFVARN(king->RAINBOWActiveBlockCount, "RAINBOWActiveBlockCount"),
  SFVARN(king->RAINBOWConfigPending, "RAINBOWConfigPending"),
  SFVARN(king->RAINBOWConfigStateVersion, "RAINBOWConfigStateVersion"),

  SFVARN(king->RAINBOWStartPending, "RAINBOWStartPending"),
  SFVARN(king->RAINBOWBusyCount, "RAINBOWBusyCount"),
  SFVARN(king->RAINBOWBlockCount, "RAINBOWBlockCount"),

  SFVARN(king->RasterIRQLine, "RasterIRQLine"),
  SFVARN(king->RasterIRQPending, "RasterIRQPending"),
  SFVARN(king->RAINBOWKRAMReadPos, "RAINBOWKRAMReadPos"),
  SFARRAY32N(king->RainbowAheadAddress,3,"RainbowAheadAddress"),
  SFARRAY32N(king->RainbowAheadGrantAt,3,"RainbowAheadGrantAt"),
  SFARRAY16N(king->RainbowAheadValue,3,"RainbowAheadValue"),
  SFARRAYN(king->RainbowAheadPage,3,"RainbowAheadPage"),
  SFARRAYN(king->RainbowAheadStatus,3,"RainbowAheadStatus"),
  SFVARN(king->RainbowAheadStateVersion,"RainbowAheadStateVersion"),
  SFARRAYN(king->RBAsyncStage,131072,"RBAsyncStage"),
  SFVARN(king->RBAsyncLength,"RBAsyncLength"),
  SFVARN(king->RBAsyncProducerPos,"RBAsyncProducerPos"),
  SFVARN(king->RBAsyncConsumeIndex,"RBAsyncConsumeIndex"),
  SFVARN(king->RBAsyncScanIndex,"RBAsyncScanIndex"),
  SFVARN(king->RBAsyncTarget,"RBAsyncTarget"),
  SFVARN(king->RBAsyncGrantAt,"RBAsyncGrantAt"),
  SFVARN(king->RBAsyncHeaderRemaining,"RBAsyncHeaderRemaining"),
  SFVARN(king->RBAsyncSkipRemaining,"RBAsyncSkipRemaining"),
  SFVARN(king->RBAsyncWords,"RBAsyncWords"),
  SFVARN(king->RBAsyncWaits,"RBAsyncWaits"),
  SFVARN(king->RBAsyncUnderflows,"RBAsyncUnderflows"),
  SFVARN(king->RBAsyncStageVersion,"RBAsyncStageVersion"),
  SFVARN(king->RBAsyncActive,"RBAsyncActive"),
  SFVARN(king->RBAsyncPending,"RBAsyncPending"),
  SFVARN(king->RBAsyncDecoding,"RBAsyncDecoding"),
  SFVARN(king->RBAsyncFirst,"RBAsyncFirst"),
  SFVARN(king->RBAsyncSkip,"RBAsyncSkip"),
  SFVARN(king->RBAsyncScanState,"RBAsyncScanState"),
  SFVARN(king->RBAsyncMarker,"RBAsyncMarker"),
  SFVARN(king->RBAsyncSizeHigh,"RBAsyncSizeHigh"),
  SFVARN(king->RBAsyncPage,"RBAsyncPage"),
  SFVARN(king->RBAsyncAddress,"RBAsyncAddress"),
  SFVARN(king->RBAsyncRLEVersion,"RBAsyncRLEVersion"),
  SFVARN(king->RBAsyncRLEActive,"RBAsyncRLEActive"),
  SFVARN(king->RBAsyncRLECursor,"RBAsyncRLECursor"),
  SFVARN(king->RBAsyncRLEEnd,"RBAsyncRLEEnd"),
  SFVARN(king->RBAsyncRLEX,"RBAsyncRLEX"),
  SFVARN(king->RBAsyncRLETokenCount,"RBAsyncRLETokenCount"),
  SFVARN(king->RBAsyncRLETotalTokens,"RBAsyncRLETotalTokens"),
  SFVARN(king->RBAsyncRLEBlocks,"RBAsyncRLEBlocks"),
  SFVARN(king->RBJPEGVersion,"RBJPEGVersion"),
  SFVARN(king->RBJPEGActive,"RBJPEGActive"),
  SFVARN(king->RBJPEGQuantExpected,"RBJPEGQuantExpected"),
  SFVARN(king->RBJPEGQuantCount,"RBJPEGQuantCount"),
  SFARRAYN(king->RBJPEGQuant,128,"RBJPEGQuant"),
  SFVARN(king->RBJPEGCursor,"RBJPEGCursor"),
  SFVARN(king->RBJPEGEntropyEnd,"RBJPEGEntropyEnd"),
  SFVARN(king->RBJPEGEntropyBytes,"RBJPEGEntropyBytes"),
  SFVARN(king->RBJPEGStuffedPairs,"RBJPEGStuffedPairs"),
  SFVARN(king->RBJPEGStalledPairs,"RBJPEGStalledPairs"),
  SFVARN(king->RBJPEGBlocks,"RBJPEGBlocks"),
  SFVARN(king->RBJPEGDigest,"RBJPEGDigest"),
  SFVARN(king->RBHVersion,"RBHVersion"),
  SFVARN(king->RBHStarted,"RBHStarted"),
  SFVARN(king->RBHCompletedBlocks,"RBHCompletedBlocks"),
  SFARRAYN((uint8*)&king->RBH,sizeof(king->RBH),"RBHState"),
  SFARRAYN(king->RBMCULuma,4096,"RBMCULuma"),
  SFARRAYN(king->RBMCUValidColumn,16,"RBMCUValidColumn"),
  SFARRAYN(king->RBMCUQuantBase,64,"RBMCUQuantBase"),
  SFVARN(king->RBMCUQuantValid,"RBMCUQuantValid"),
  SFVARN(king->RBMCUScale,"RBMCUScale"),
  SFVARN(king->RBMCUVersion,"RBMCUVersion"),
  SFVARN(king->RBMCUColumns,"RBMCUColumns"),
  SFVARN(king->RBMCUCompared,"RBMCUCompared"),
  SFVARN(king->RBMCUDifferences,"RBMCUDifferences"),
  SFVARN(king->RBMCUBlocks,"RBMCUBlocks"),
  SFVARN(king->RBMCUExactBlocks,"RBMCUExactBlocks"),
  SFVARN(king->DMATransferFlipFlop, "DMATransferFlipFlop"),
  SFVARN(king->DMATransferAddr, "DMATransferAddr"),
  SFVARN(king->DMATransferSize, "DMATransferSize"),
  SFVARN(king->DMAStatus, "DMAStatus"),
  SFVARN(king->DMAInterrupt, "DMAInterrupt"),
  SFVARN(king->DMALatch, "DMALatch"),
  SFVARN(king->DMARetirePending, "DMARetirePending"),
  SFVARN(king->PIOReadSeen, "PIOReadSeen"),
  SFVARN(king->PIOBusWedged, "PIOBusWedged"),
  SFVARN(king->MPROGControl, "MPROGControl"),
  SFVARN(king->MPROGAddress, "MPROGAddress"),
  SFARRAY16N(king->MPROGData, 16, "MPROGData"),
  SFVARN(king->Reg00, "Port00"),
  SFVARN(king->Reg01, "Port01"),
  SFVARN(king->Reg02, "Port02"),
  SFVARN(king->Reg03, "Port03"),
  SFVARN(king->CDInterrupt, "CDInterrupt"),
  SFVARN(king->data_cache, "data_cache"),
  SFVARN(king->DRQ, "DRQ"),
  SFVARN(king->dma_receive_active, "dma_receive_active"),
  SFVARN(king->dma_send_active, "dma_send_active"),
  SFVARN(king->dma_cycle_counter, "dma_cycle_counter"),
  SFVARN(king->KRAMWriteBusyUntil, "KRAMWriteBusyUntil"),
  SFVARN(king->KRAMWritePending, "KRAMWritePending"),
  SFVARN(king->KRAMWritePendingPage, "KRAMWritePendingPage"),
  SFVARN(king->KRAMWritePendingAddress, "KRAMWritePendingAddress"),
  SFVARN(king->KRAMWritePendingAt, "KRAMWritePendingAt"),
  SFARRAY32N(&king->KBusSlotTime[0][0], 2*KING_KBUS_SLOTS, "KBusSlotTime"),
  SFARRAYN(&king->KBusSlotSource[0][0], 2*KING_KBUS_SLOTS, "KBusSlotSource"),
  SFVARN(king->KBusStateVersion, "KBusStateVersion"),
  SFARRAY16N(&king->KBusSlotTag[0][0],2*KING_KBUS_SLOTS,"KBusSlotTag"),
  SFARRAY32N(king->SCSIPendingAddress,KING_SCSI_PENDING_MAX,"SCSIPendingAddress"),
  SFARRAY32N(king->SCSIPendingGrantAt,KING_SCSI_PENDING_MAX,"SCSIPendingGrantAt"),
  SFARRAY16N(king->SCSIPendingValue,KING_SCSI_PENDING_MAX,"SCSIPendingValue"),
  SFARRAYN(king->SCSIPendingPage,KING_SCSI_PENDING_MAX,"SCSIPendingPage"),
  SFARRAYN(king->SCSIPendingValid,KING_SCSI_PENDING_MAX,"SCSIPendingValid"),
  SFVARN(king->SCSIPendingStateVersion,"SCSIPendingStateVersion"),
  SFVARN(king->SCSIPendingCount,"SCSIPendingCount"),
  SFARRAY32N(king->ADPCMGrantAddress,2,"ADPCMGrantAddress"),
  SFARRAY32N(king->ADPCMGrantAt,2,"ADPCMGrantAt"),
  SFARRAYN(king->ADPCMGrantPage,2,"ADPCMGrantPage"),
  SFARRAYN(king->ADPCMGrantPending,2,"ADPCMGrantPending"),
  SFVARN(king->ADPCMGrantStateVersion,"ADPCMGrantStateVersion"),
  SFVARN(king->KRAMWritePendingValue, "KRAMWritePendingValue"),
  SFVARN(king->KRAMWritePendingStateVersion, "KRAMWritePendingStateVersion"),

  SFVARN(king->SubChannelBuf, "SubChannelBuf"),
  SFVARN(king->SubChannelInterrupt, "SubChannelInterrupt"),
  SFVARN(king->SubChannelControl, "SubChannelControl"),

  SFVAR(scsicd_ne),
  // scsicd_ne here??
  SFEND
 };

 SFORMAT VCEStateRegs[] =
 {
  SFVARN(fx_vce.AR, "AR"),
  SFARRAY16N(fx_vce.priority, 2, "priority"),
  SFVARN(fx_vce.odd_field, "odd_field"),
  SFVARN(fx_vce.in_hblank, "in_hblank"),
  SFVARN(fx_vce.in_vdc_hsync, "in_vdc_hsync"),
  SFVARN(fx_vce.picture_mode, "picture_mode"),
  SFVARN(fx_vce.active_picture_mode, "active_picture_mode"),
  SFVARN(vce_state_latch_version, "vce_latch_version"),

  SFVARN(HPhase, "HPhase"),
  SFVARN(HPhaseCounter, "HPhaseCounter"),
  SFVAR(vdc_lb_pos),

  SFVARN(fx_vce.dot_clock, "dot_clock"),
  SFVARN(fx_vce.clock_divider, "clock_divider"),

  SFARRAY32N(fx_vce.vdc_event, 2, "vdc_event"),

  SFVARN(fx_vce.raster_counter, "raster_counter"),
  SFVARN(fx_vce.palette_rw_offset, "palette_rw_offset"),
  SFVARN(fx_vce.palette_rw_latch, "palette_rw_latch"),
  SFARRAY16N(fx_vce.palette_offset, 4, "palette_offset"),
  SFARRAY16N(fx_vce.palette_table, 512, "palette_table"),
  SFVARN(fx_vce.ChromaKeyY, "ChromaKeyY"),
  SFVARN(fx_vce.ChromaKeyU, "ChromaKeyU"),
  SFVARN(fx_vce.ChromaKeyV, "ChromaKeyV"),
  SFVARN(fx_vce.CCR, "CCR"),
  SFVARN(fx_vce.BLE, "BLE"),
  SFVARN(fx_vce.SPBL, "SPBL"),
  SFARRAY16N(fx_vce.coefficients, 6, "coefficients"),


  // HB render cache:
  // FIXME
  SFARRAY16N(vce_rendercache.priority, 2, "rc_priority"),
  SFVARN(vce_rendercache.picture_mode, "rc_picture_mode"),
  SFARRAY16N(vce_rendercache.palette_offset, 4, "rc_palette_offset"),
  SFVARN(vce_rendercache.ChromaKeyY, "rc_ChromaKeyY"),
  SFVARN(vce_rendercache.ChromaKeyU, "rc_ChromaKeyU"),
  SFVARN(vce_rendercache.ChromaKeyV, "rc_ChromaKeyV"),
  SFVARN(vce_rendercache.CCR, "rc_CCR"),
  SFVARN(vce_rendercache.BLE, "rc_BLE"),
  SFVARN(vce_rendercache.SPBL, "rc_SPBL"),
  SFARRAY16N(vce_rendercache.coefficients, 6, "rc_coefficients"),

  SFEND
 };

 /* Older save states lack the separately latched VCE mode register. Do not
    display a black field merely because that field was absent. */
 vce_state_latch_version = load ? 0 : 1;
 king->RAINBOWConfigStateVersion = load ? 0 : 1;
 king->ADPCMTransferLatchVersion = load ? 0 : 1;
 king->KRAMWritePendingStateVersion = load ? 0 : 1;
 king->KBusStateVersion = load ? 0 : 1;
 king->SCSIPendingStateVersion = load ? 0 : 1;
 king->ADPCMGrantStateVersion = load ? 0 : 1;
 king->RainbowAheadStateVersion=load?0:1;
 king->RBAsyncStageVersion=load?0:1;
 king->RBAsyncRLEVersion=load?0:1;
 king->RBJPEGVersion=load?0:1;
 king->RBHVersion=load?0:1;
 king->RBMCUVersion=load?0:1;
 int ret = MDFNSS_StateAction(sm, load, data_only, KINGStateRegs, "KING", false);
 
 ret &= MDFNSS_StateAction(sm, load, data_only, VCEStateRegs, "VCE", false);

 if(load)
 {
  if(!king->RBAsyncStageVersion)
  {
   king->RBAsyncActive=king->RBAsyncPending=king->RBAsyncDecoding=0;
   king->RBAsyncLength=king->RBAsyncScanIndex=king->RBAsyncTarget=0;
   king->RBAsyncProducerPos=king->RAINBOWKRAMReadPos;
  }
  king->RBAsyncStageVersion=1;
  if(!king->RBAsyncRLEVersion) king->RBAsyncRLEActive=0;
  king->RBAsyncRLEVersion=1;
  if(!king->RBJPEGVersion) king->RBJPEGActive=0;
  if(!king->RBHVersion) { king->RBHStarted=0; memset(&king->RBH,0,sizeof(king->RBH)); }
  king->RBHVersion=1;
  if(!king->RBMCUVersion)
  {
   king->RBMCUQuantValid=0;
   king->RBMCUScale=4;
   memset(king->RBMCUValidColumn,0,sizeof(king->RBMCUValidColumn));
  }
  king->RBMCUVersion=1;
  if(king->RBHStarted && (king->RBH.cursor>king->RBAsyncLength ||
       king->RBH.end>sizeof(king->RBAsyncStage) || king->RBH.bits>24 ||
       king->RBH.component>=6 || king->RBH.column>16 ||
       king->RBH.ac_index>63)) king->RBHStarted=0;

  king->RBJPEGVersion=1;
  if(king->RBJPEGCursor>king->RBAsyncLength ||
     king->RBJPEGEntropyEnd>sizeof(king->RBAsyncStage) ||
     king->RBJPEGQuantCount>128 ||
     (king->RBJPEGQuantExpected!=0 && king->RBJPEGQuantExpected!=128))
    king->RBJPEGActive=0;
  if(!king->RBAsyncActive) king->RBJPEGActive=0;
  if(king->RBAsyncRLECursor>king->RBAsyncLength ||
     king->RBAsyncRLEEnd>sizeof(king->RBAsyncStage) ||
     king->RBAsyncRLEX>0x2000) king->RBAsyncRLEActive=0;
  if(king->RBAsyncLength>sizeof(king->RBAsyncStage)) king->RBAsyncLength=0;
  if(king->RBAsyncScanIndex>king->RBAsyncLength) king->RBAsyncScanIndex=0;
  king->RBAsyncActive&=1;
  king->RBAsyncPending&=1;
  king->RBAsyncDecoding=0;
  king->RBAsyncPage&=1;
  if(!king->RainbowAheadStateVersion)
   memset(king->RainbowAheadStatus,0,sizeof(king->RainbowAheadStatus));
  king->RainbowAheadStateVersion=1;
  for(unsigned j=0;j<3;j++)
  {
   if(king->RainbowAheadStatus[j]>2) king->RainbowAheadStatus[j]=0;
   king->RainbowAheadPage[j]&=1;
   king->RainbowAheadAddress[j]&=0x3FFFFu;
  }
  if(!king->ADPCMGrantStateVersion)
  {
   memset(king->ADPCMGrantPending,0,sizeof(king->ADPCMGrantPending));
  }
  king->ADPCMGrantStateVersion=1;
  for(unsigned ch=0;ch<2;ch++)
  {
   king->ADPCMGrantPending[ch]&=1;
   king->ADPCMGrantPage[ch]&=1;
   king->ADPCMGrantAddress[ch]&=0x3FFFFu;
  }
  if(!king->SCSIPendingStateVersion)
  {
   memset(king->SCSIPendingValid,0,sizeof(king->SCSIPendingValid));
   memset(king->KBusSlotTag,0,sizeof(king->KBusSlotTag));
  }
  king->SCSIPendingStateVersion=1;
  king->SCSIPendingCount=0;
  for(unsigned n=0;n<KING_SCSI_PENDING_MAX;n++)
  {
   king->SCSIPendingValid[n]&=1;
   king->SCSIPendingCount+=king->SCSIPendingValid[n];
   king->SCSIPendingPage[n]&=1;
   king->SCSIPendingAddress[n]&=0x3FFFFu;
  }
  if(!king->KBusStateVersion)
  {
   memset(king->KBusSlotTime,0,sizeof(king->KBusSlotTime));
   memset(king->KBusSlotSource,0,sizeof(king->KBusSlotSource));
  }
  king->KBusStateVersion = 1;
  if(!king->KRAMWritePendingStateVersion)
  {
   /* Older states already committed their KRAM writes eagerly. */
   king->KRAMWritePending = 0;
  }
  king->KRAMWritePendingStateVersion = 1;
  king->KRAMWritePending &= 1u;
  king->KRAMWritePendingPage &= 1u;
  king->KRAMWritePendingAddress &= 0x3FFFFu;
  if(!king->ADPCMTransferLatchVersion)
  {
   king->ADPCMControlWritten = king->ADPCMControl;
   king->ADPCMPendingEnable = 0;
  }
  king->ADPCMTransferLatchVersion = 1;
  king->ADPCMControl &= 0xFu;
  king->ADPCMControlWritten &= 0xFu;
  king->ADPCMPendingEnable &= 3u;
  if(!king->RAINBOWConfigStateVersion)
  {
   /* Pre-v5 states only have the CPU-facing REG 41/43 values. */
   king->RAINBOWActiveKRAMA = king->RAINBOWKRAMA;
   king->RAINBOWActiveBlockCount = king->RAINBOWTransferBlockCount;
   king->RAINBOWConfigPending = 0;
  }
  king->RAINBOWConfigStateVersion = 1;
  king->RAINBOWKRAMA &= 0x3FFFFu;
  king->RAINBOWActiveKRAMA &= 0x3FFFFu;
  king->RAINBOWTransferBlockCount &= 0x1Fu;
  king->RAINBOWActiveBlockCount &= 0x1Fu;
  king->RAINBOWConfigPending &= 3u;
  if(!vce_state_latch_version)
  {
   fx_vce.active_picture_mode = fx_vce.picture_mode;
   vce_rendercache.picture_mode = fx_vce.active_picture_mode;
  }
  vce_state_latch_version = 1;
  RecalcKRAMPagePtrs();

  fx_vce.dot_clock_ratio = fx_vce.dot_clock ? 3 : 4;

  fx_vce.palette_rw_offset &= 0x1FF;
  fx_vce.palette_offset[3] &= 0x00FF;
  fx_vce.priority[0] &= 0x0777;
  fx_vce.priority[1] &= 0x7777;

  for(int x = 0; x < 6; x++)
   fx_vce.coefficients[x] &= 0x0FFF;

  for(int x = 0; x < 0x200; x++)
   RedoPaletteCache(x);

  vdc_lb_pos &= 0x1FF; // FIXME: Better checks(in case we remove the assert() elsewhere)?

  RedoKINGIRQCheck();
  SoundBox_SetKINGADPCMControl(king->ADPCMControl);
 }
 return(ret);
}
