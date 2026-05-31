#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>
#if !defined(_WIN32) || !defined(PCFX_EXTERNAL_FRONTEND)
#include <libgen.h>
#endif
#define _BSD_SOURCE
#include <sys/time.h>

#ifdef _MSC_VER

#endif


#include "mednafen/mednafen.h"
#include "mednafen/git.h"
#include "mednafen/md5.h"


#include "mednafen/pcfx/pcfx.h"
#include "mednafen/pcfx/soundbox.h"
#include "mednafen/pcfx/input.h"
#include "mednafen/pcfx/king.h"
#include "mednafen/pcfx/timer.h"
#include "mednafen/pcfx/interrupt.h"
#include "mednafen/hw_cpu/v810/v810_cpu.h"
#include "mednafen/pcfx/rainbow.h"
#ifdef HAVE_HUC6273
#include "mednafen/pcfx/huc6273.h"
#endif
#include "mednafen/cdrom/scsicd.h"
#include "mednafen/cdrom/cdromif.h"
#include "mednafen/md5.h"
#include "mednafen/clamp.h"
#include "mednafen/state_helpers.h"



#include "video_blit.h"
#include "sound_output.h"
#include "input_emu.h"
#include "main.h"
#include "shared.h"
#include "menu.h"
#include "config.h"

char GameName_emu[256];
uint8_t exit_vb = 0;
extern uint32_t emulator_state;

static char pcfx_base_directory[2048];
static char pcfx_save_directory[2048];

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

/* FIXME:  soundbox, vce, vdc, rainbow, and king store wait states should be 4, not 2, but V810 has write buffers which can mask wait state penalties.
  This is a hack to somewhat address the issue, but to really fix it, we need to handle write buffer emulation in the V810 emulation core itself.
*/

typedef struct CDIFList
{
   CDIF* items[16];
   unsigned count;
} CDIFList;

static void CDIFList_Clear(CDIFList* list)
{
   if(!list) return;
   for(unsigned i = 0; i < list->count; i++)
   {
      if(list->items[i])
         CDIF_Close_C(list->items[i]);
      list->items[i] = NULL;
   }
   list->count = 0;
}

static int CDIFList_Push(CDIFList* list, CDIF* cdif)
{
   if(!list || !cdif || list->count >= 16) return 0;
   list->items[list->count++] = cdif;
   return 1;
}

static CDIFList *cdifs = NULL;
static bool CD_TrayOpen;
static int CD_SelectedDisc;	// -1 for no disc


static uint8 *BIOSROM = NULL; 	// 1MB
static uint8 *RAM = NULL; 	// 2MB
static void PCFX_PIOCloseAll(void);
static bool PCFX_PIOTrap(uint32 trap_id);

static uint32 RAM_LPA;		// Last page access

static const int RAM_PageSize = 2048;
static const int RAM_PageNOTMask = ~(RAM_PageSize - 1);

static uint16 Last_VDC_AR[2];

#ifdef HAVE_HUC6273
static bool WantHuC6273 = false;
static bool AllowHuC6273 = true;

void PCFX_SetHuC6273Enabled(bool enabled)
{
 AllowHuC6273 = enabled;
 WantHuC6273 = enabled;
}

bool PCFX_GetHuC6273Enabled(void)
{
 return AllowHuC6273;
}

bool PCFX_HuC6273Active(void)
{
 return WantHuC6273 && AllowHuC6273;
}

static void PCFX_ApplyHuC6273Presence(void)
{
 /* The HuC6273/Aurora is an optional 3D device, not a BIOS-mode side effect.
  * Frontends expose it as a separate 3D-chip toggle, so explicit PC-FX and
  * explicit PC-FXGA modes must not implicitly force it off or on.  BIOS mode
  * selection only chooses the boot ROM and related startup path; this flag
  * controls whether the I/O/memory mapped 3D chip is visible to software. */
 WantHuC6273 = AllowHuC6273;
}
#endif

typedef enum PCFXBIOSKind
{
 PCFX_BIOS_UNKNOWN = 0,
 PCFX_BIOS_CONSOLE_100,
 PCFX_BIOS_CONSOLE_101,
 PCFX_BIOS_FXGA
} PCFXBIOSKind;

static PCFXBIOSKind CurrentBIOSKind = PCFX_BIOS_UNKNOWN;
static char CurrentBIOSPath[512];
static char CurrentBIOSMD5[33];

/* Optional in-memory patches for the original PC-FX console BIOS.
 * Patch data is derived from PC-FX_Bios_Patches by pcfx-devel, MIT licensed.
 * They are applied only to known 1MB PC-FX console BIOS dumps after loading
 * the BIOS into emulator memory; the file on disk is never modified. */
static uint32 PCFXBIOSPatchFlags = 0;

#ifdef PCFX_ADPCM_COMPAT_OPTIONS
static int PCFXADPCMBuggyCodecMode = PCFX_ADPCM_BUGGY_AUTO;
static bool PCFXADPCMSuppressResetClicks = true;
static bool PCFXADPCMGameNeedsBuggyCodec = false;
#endif

void PCFX_SetBIOSPatches(uint32_t flags)
{
 PCFXBIOSPatchFlags = flags & (PCFX_BIOS_PATCH_SHORTINTRO | PCFX_BIOS_PATCH_ENGLISH | PCFX_BIOS_PATCH_AUTOLAUNCH);
}

uint32_t PCFX_GetBIOSPatches(void)
{
 return PCFXBIOSPatchFlags;
}

#ifdef PCFX_ADPCM_COMPAT_OPTIONS
static void PCFX_ApplyADPCMCompatOptions(void)
{
 bool emulate_buggy_codec = false;
 if(PCFXADPCMBuggyCodecMode == PCFX_ADPCM_BUGGY_ON)
  emulate_buggy_codec = true;
 else if(PCFXADPCMBuggyCodecMode == PCFX_ADPCM_BUGGY_AUTO)
  emulate_buggy_codec = PCFXADPCMGameNeedsBuggyCodec;
 SoundBox_SetADPCMOptions(emulate_buggy_codec, PCFXADPCMSuppressResetClicks);
}

void PCFX_SetADPCMOptions(bool emulate_buggy_codec, bool suppress_channel_reset_clicks)
{
 PCFXADPCMBuggyCodecMode = emulate_buggy_codec ? PCFX_ADPCM_BUGGY_ON : PCFX_ADPCM_BUGGY_OFF;
 PCFXADPCMSuppressResetClicks = suppress_channel_reset_clicks;
 PCFX_ApplyADPCMCompatOptions();
}

void PCFX_SetADPCMCompatOptions(int buggy_codec_mode, bool suppress_channel_reset_clicks)
{
 if(buggy_codec_mode < PCFX_ADPCM_BUGGY_AUTO || buggy_codec_mode > PCFX_ADPCM_BUGGY_ON)
  buggy_codec_mode = PCFX_ADPCM_BUGGY_AUTO;
 PCFXADPCMBuggyCodecMode = buggy_codec_mode;
 PCFXADPCMSuppressResetClicks = suppress_channel_reset_clicks;
 PCFX_ApplyADPCMCompatOptions();
}

int PCFX_GetADPCMBuggyCodecMode(void)
{
 return PCFXADPCMBuggyCodecMode;
}

bool PCFX_GetADPCMEmulateBuggyCodec(void)
{
 return SoundBox_GetADPCMEmulateBuggyCodec();
}

bool PCFX_GetADPCMSuppressChannelResetClicks(void)
{
 return SoundBox_GetADPCMSuppressChannelResetClicks();
}
#endif

void PCFX_SetCDSpeed(uint_fast32_t speed)
{
 MDFN_SetPCFXCDSpeed(speed);
 SCSICD_SetTransferRate(153600 * setting_cd_speed);
}

uint_fast32_t PCFX_GetCDSpeed(void)
{
 return MDFN_GetPCFXCDSpeed();
}


static const uint8 PCFX_BIOS_PATCH_ENGLISH_DATA[499] =
{
 0x18, 0x14, 0x4D, 0x75, 0x73, 0x69, 0x63, 0x20, 0x20, 0x43, 0x44, 0x20, 0x20, 0x4C, 0x6F, 0x61,
 0x64, 0x65, 0x64, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x18, 0x20, 0x20, 0x50, 0x43, 0x2D, 0x46,
 0x58, 0x20, 0x20, 0x43, 0x44, 0x2D, 0x52, 0x4F, 0x4D, 0x20, 0x20, 0x4C, 0x6F, 0x61, 0x64, 0x65,
 0x64, 0x20, 0x20, 0x20, 0x20, 0x00, 0x1C, 0x50, 0x68, 0x6F, 0x74, 0x6F, 0x20, 0x20, 0x43, 0x44,
 0x20, 0x20, 0x4C, 0x6F, 0x61, 0x64, 0x65, 0x64, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00,
 0x20, 0x20, 0x50, 0x43, 0x20, 0x20, 0x45, 0x6E, 0x67, 0x69, 0x6E, 0x65, 0x20, 0x20, 0x43, 0x44,
 0x2D, 0x52, 0x4F, 0x4D, 0x20, 0x20, 0x4C, 0x6F, 0x61, 0x64, 0x65, 0x64, 0x00, 0x07, 0x18, 0x20,
 0x20, 0x4E, 0x6F, 0x74, 0x20, 0x20, 0x61, 0x20, 0x20, 0x50, 0x43, 0x2D, 0x46, 0x58, 0x20, 0x20,
 0x43, 0x44, 0x2D, 0x52, 0x4F, 0x4D, 0x20, 0x20, 0x20, 0x00, 0x18, 0x50, 0x6C, 0x65, 0x61, 0x73,
 0x65, 0x20, 0x20, 0x49, 0x6E, 0x73, 0x65, 0x72, 0x74, 0x20, 0x20, 0x44, 0x69, 0x73, 0x63, 0x20,
 0x20, 0x31, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x18, 0x20, 0x50, 0x6C, 0x65, 0x61,
 0x73, 0x65, 0x20, 0x20, 0x49, 0x6E, 0x73, 0x65, 0x72, 0x74, 0x20, 0x20, 0x44, 0x69, 0x73, 0x63,
 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x10, 0x13, 0x50, 0x6C, 0x65, 0x61, 0x73, 0x65,
 0x20, 0x20, 0x43, 0x6C, 0x6F, 0x73, 0x65, 0x20, 0x20, 0x4C, 0x69, 0x64, 0x20, 0x20, 0x20, 0x20,
 0x00, 0x1C, 0x1C, 0x4C, 0x6F, 0x61, 0x64, 0x69, 0x6E, 0x67, 0x20, 0x2E, 0x2E, 0x2E, 0x20, 0x20,
 0x20, 0x20, 0x20, 0x00, 0x0E, 0x43, 0x68, 0x65, 0x63, 0x6B, 0x69, 0x6E, 0x67, 0x20, 0x20, 0x44,
 0x69, 0x73, 0x63, 0x20, 0x20, 0x46, 0x6F, 0x72, 0x6D, 0x61, 0x74, 0x20, 0x20, 0x20, 0x20, 0x20,
 0x20, 0x00, 0x18, 0x20, 0x20, 0x4E, 0x6F, 0x74, 0x20, 0x20, 0x61, 0x20, 0x20, 0x50, 0x43, 0x2D,
 0x46, 0x58, 0x20, 0x20, 0x43, 0x44, 0x2D, 0x52, 0x4F, 0x4D, 0x20, 0x20, 0x20, 0x20, 0x00, 0x10,
 0x13, 0x55, 0x6E, 0x61, 0x62, 0x6C, 0x65, 0x20, 0x20, 0x54, 0x6F, 0x20, 0x20, 0x4C, 0x6F, 0x61,
 0x64, 0x20, 0x20, 0x44, 0x69, 0x73, 0x63, 0x00, 0x18, 0x18, 0x20, 0x20, 0x50, 0x6C, 0x65, 0x61,
 0x73, 0x65, 0x20, 0x20, 0x53, 0x65, 0x6C, 0x65, 0x63, 0x74, 0x00, 0x18, 0x18, 0x43, 0x44, 0x2F,
 0x43, 0x44, 0x2D, 0x47, 0x20, 0x20, 0x50, 0x6C, 0x61, 0x79, 0x65, 0x72, 0x20, 0x20, 0x20, 0x20,
 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x1C, 0x10, 0x50, 0x68, 0x6F, 0x74, 0x6F, 0x20, 0x20,
 0x43, 0x44, 0x20, 0x20, 0x50, 0x6C, 0x61, 0x79, 0x65, 0x72, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
 0x00, 0x18, 0x18, 0x4C, 0x6F, 0x61, 0x64, 0x20, 0x20, 0x50, 0x43, 0x2D, 0x46, 0x58, 0x20, 0x20,
 0x44, 0x69, 0x73, 0x63, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x18, 0x10,
 0x46, 0x69, 0x6C, 0x65, 0x20, 0x20, 0x4D, 0x61, 0x69, 0x6E, 0x74, 0x65, 0x6E, 0x61, 0x6E, 0x63,
 0x65, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x18, 0x18, 0x10, 0x43, 0x68, 0x61, 0x6E, 0x67,
 0x65, 0x20, 0x20, 0x44, 0x69, 0x73, 0x63, 0x00, 0x10, 0x10, 0x50, 0x6C, 0x65, 0x61, 0x73, 0x65,
 0x20, 0x20, 0x43, 0x68, 0x61, 0x6E, 0x67, 0x65, 0x20, 0x20, 0x44, 0x69, 0x73, 0x63, 0x20, 0x20,
 0x20, 0x20, 0x20
};

static void PCFX_BIOSPatchCopy(uint32 offset, const uint8* data, uint32 size)
{
 if(!BIOSROM || !data || !size)
  return;
 if(offset >= 1024 * 1024 || size > (1024 * 1024) - offset)
  return;
 memcpy(BIOSROM + offset, data, size);
}

static void PCFX_BIOSPatchByte(uint32 offset, uint8 value)
{
 PCFX_BIOSPatchCopy(offset, &value, 1);
}

static void PCFX_ApplyBIOSPatchesToLoadedROM(void)
{
 if(!BIOSROM)
  return;

 /* These offsets are valid for the known original console BIOS revisions.
  * Do not apply them to the PC-FXGA BIOS or to unknown 1MB images. */
 if(CurrentBIOSKind != PCFX_BIOS_CONSOLE_100 && CurrentBIOSKind != PCFX_BIOS_CONSOLE_101)
  return;

 if(PCFXBIOSPatchFlags & PCFX_BIOS_PATCH_SHORTINTRO)
 {
  static const uint8 p0[4] = { 0x00, 0xAC, 0x0E, 0x02 };
  static const uint8 p1[4] = { 0xE3, 0x40, 0xE3, 0x40 };
  PCFX_BIOSPatchCopy(0x003C98, p0, sizeof(p0));
  PCFX_BIOSPatchCopy(0x003EDA, p1, sizeof(p1));
  PCFX_BIOSPatchByte(0x003F26, 0x02);
 }

 if(PCFXBIOSPatchFlags & PCFX_BIOS_PATCH_ENGLISH)
  PCFX_BIOSPatchCopy(0x009438, PCFX_BIOS_PATCH_ENGLISH_DATA, sizeof(PCFX_BIOS_PATCH_ENGLISH_DATA));

 if(PCFXBIOSPatchFlags & PCFX_BIOS_PATCH_AUTOLAUNCH)
  PCFX_BIOSPatchByte(0x004171, 0x00);
}

enum
{
 PCFX_SYSTEM_MODE_PCFX = 0,
 PCFX_SYSTEM_MODE_FXGA = 1,
 PCFX_SYSTEM_MODE_AUTO = 2
};

static int PCFXSystemMode = PCFX_SYSTEM_MODE_PCFX;
static int PCFX_NormalizeSystemMode(int mode)
{
 if(mode == PCFX_SYSTEM_MODE_FXGA || mode == PCFX_SYSTEM_MODE_AUTO)
  return mode;
 return PCFX_SYSTEM_MODE_PCFX;
}
void PCFX_SetSystemMode(int mode) { PCFXSystemMode = PCFX_NormalizeSystemMode(mode); }
void PCFX_SetPreferFXGABIOS(bool enabled) { PCFX_SetSystemMode(enabled ? PCFX_SYSTEM_MODE_FXGA : PCFX_SYSTEM_MODE_PCFX); }
static bool PCFX_SystemModeForcesFXGA(void) { return PCFXSystemMode == PCFX_SYSTEM_MODE_FXGA; }
static bool PCFX_SystemModeIsAuto(void) { return PCFXSystemMode == PCFX_SYSTEM_MODE_AUTO; }

static bool LoadingHuEXE = false;
static bool PendingHuEXEUpload = false;
static unsigned PendingHuEXEUploadFrames = 0;
static uint32 PendingHuEXEStartPC = 0;
static uint8* PendingHuEXEImage = NULL;
static size_t PendingHuEXEImageSize = 0;

#ifndef PCFXGA_HUEXE_UPLOAD_DELAY_FRAMES
#define PCFXGA_HUEXE_UPLOAD_DELAY_FRAMES 120
#endif

//static 
VDC *fx_vdc_chips[2];

static uint16 BackupControl;
static uint8 SaveRAM[2 * 0x8000]; // BackupRAM + ExBackupRAM
static uint8* BackupRAM = (uint8*)(SaveRAM + (0x8000 * 0));
static uint8* ExBackupRAM = (uint8*)(SaveRAM + (0x8000 * 1));
static uint8 ExBusReset; // I/O Register at 0x0700

//static bool BRAMDisabled;	// Cached at game load, don't remove this caching behavior or save game loss may result(if we ever get a GUI).

// Checks to see if this main-RAM-area access
// is in the same DRAM page as the last access.
#define RAMLPCHECK	\
{					\
  if((A & RAM_PageNOTMask) != RAM_LPA)	\
  {					\
   (*timestamp) += 3;			\
   RAM_LPA = A & RAM_PageNOTMask;	\
  }					\
}

static v810_timestamp_t next_pad_ts, next_timer_ts, next_adpcm_ts, next_king_ts;

void PCFX_FixNonEvents(void)
{
	if(next_pad_ts & 0x40000000)
		next_pad_ts = PCFX_EVENT_NONONO;

	if(next_timer_ts & 0x40000000)
		next_timer_ts = PCFX_EVENT_NONONO;

	if(next_adpcm_ts & 0x40000000)
		next_adpcm_ts = PCFX_EVENT_NONONO;

	if(next_king_ts & 0x40000000)
		next_king_ts = PCFX_EVENT_NONONO;
}

void PCFX_Event_Reset(void)
{
	next_pad_ts = PCFX_EVENT_NONONO;
	next_timer_ts = PCFX_EVENT_NONONO;
	next_adpcm_ts = PCFX_EVENT_NONONO;
	next_king_ts = PCFX_EVENT_NONONO;
}

static inline uint32 CalcNextTS(void)
{
	v810_timestamp_t next_timestamp = next_king_ts;
	if(next_timestamp > next_pad_ts)
		next_timestamp  = next_pad_ts;

	if(next_timestamp > next_timer_ts)
		next_timestamp = next_timer_ts;

	if(next_timestamp > next_adpcm_ts)
		next_timestamp = next_adpcm_ts;

	return(next_timestamp);
}

static void RebaseTS(const v810_timestamp_t timestamp, const v810_timestamp_t new_base_timestamp)
{
	/*
	assert(next_pad_ts > timestamp);
	assert(next_timer_ts > timestamp);
	assert(next_adpcm_ts > timestamp);
	assert(next_king_ts > timestamp);
	*/
	next_pad_ts -= (timestamp - new_base_timestamp);
	next_timer_ts -= (timestamp - new_base_timestamp);
	next_adpcm_ts -= (timestamp - new_base_timestamp);
	next_king_ts -= (timestamp - new_base_timestamp);
	//printf("RTS: %d %d %d %d\n", next_pad_ts, next_timer_ts, next_adpcm_ts, next_king_ts);
}


void PCFX_SetEvent(const int type, const v810_timestamp_t next_timestamp)
{
	//assert(next_timestamp > v810_timestamp);
	switch(type)
	{
		case PCFX_EVENT_PAD:
			next_pad_ts = next_timestamp;
		break;
		case PCFX_EVENT_TIMER:
			next_timer_ts = next_timestamp;
		break;
		case PCFX_EVENT_ADPCM:
			next_adpcm_ts = next_timestamp;
		break;
		case PCFX_EVENT_KING:
			next_king_ts = next_timestamp;
		break;
	}
	if(next_timestamp < V810_GetEventNT())
		V810_SetEventNT(next_timestamp);
}

int32 MDFN_FASTCALL pcfx_event_handler(const v810_timestamp_t timestamp)
{
     if(timestamp >= next_king_ts)
      next_king_ts = KING_Update(timestamp);

     if(timestamp >= next_pad_ts)
      next_pad_ts = FXINPUT_Update(timestamp);

     if(timestamp >= next_timer_ts)
      next_timer_ts = FXTIMER_Update(timestamp);

     if(timestamp >= next_adpcm_ts)
      next_adpcm_ts = SoundBox_ADPCMUpdate(timestamp);

#if 0
     assert(next_king_ts > timestamp);
     assert(next_pad_ts > timestamp);
     assert(next_timer_ts > timestamp);
     assert(next_adpcm_ts > timestamp);
#endif
     return(CalcNextTS());
}

// Called externally from debug.cpp
void ForceEventUpdates(const uint32 timestamp)
{
	next_king_ts = KING_Update(timestamp);
	next_pad_ts = FXINPUT_Update(timestamp);
	next_timer_ts = FXTIMER_Update(timestamp);
	next_adpcm_ts = SoundBox_ADPCMUpdate(timestamp);
	//printf("Meow: %d\n", CalcNextTS());
	V810_SetEventNT(CalcNextTS());
	//printf("FEU: %d %d %d %d\n", next_pad_ts, next_timer_ts, next_adpcm_ts, next_king_ts);
}

#include "mednafen/pcfx/io-handler.inc"
#include "mednafen/pcfx/mem-handler.inc"

typedef struct
{
	int8 tracknum;
	int8 format;
	uint32 lba;
} CDGameEntryTrack;

typedef struct
{
	const char *name;
	const char *name_original;     // Original non-Romanized text.
	const uint32 flags;            // Emulation flags.
	const unsigned int discs;      // Number of discs for this game.
	CDGameEntryTrack tracks[2][100]; // 99 tracks and 1 leadout track
} CDGameEntry;

#define CDGE_FORMAT_AUDIO		0
#define CDGE_FORMAT_DATA		1

#define CDGE_FLAG_ACCURATE_V810         0x01
#define CDGE_FLAG_FXGA			0x02

static uint32 EmuFlags;

static CDGameEntry GameList[] =
{
	#include "mednafen/pcfx/gamedb.inc"
};


static void Emulate(EmulateSpecStruct *espec)
{
 //printf("%d\n", v810_timestamp);

 FXINPUT_Frame();

 KING_StartFrame(fx_vdc_chips, espec);	//espec->surface, &espec->DisplayRect, espec->LineWidths, espec->skip);

 v810_timestamp_t v810_timestamp;
 v810_timestamp = V810_Run(pcfx_event_handler);


 PCFX_FixNonEvents();

 // Call before resetting v810_timestamp
 ForceEventUpdates(v810_timestamp);

 //
 // Call KING_EndFrame() before SoundBox_Flush(), otherwise CD-DA audio distortion will occur due to sound data being updated
 // after it was needed instead of before.
 //
 KING_EndFrame(v810_timestamp);

 //
 // new_base_ts is guaranteed to be <= v810_timestamp
 //
 v810_timestamp_t new_base_ts = 0;
 espec->SoundBufSize = SoundBox_Flush(v810_timestamp, espec->SoundBuf, espec->SoundBufMaxSize);

 KING_ResetTS(new_base_ts);
 FXTIMER_ResetTS(new_base_ts);
 FXINPUT_ResetTS(new_base_ts);
 SoundBox_ResetTS(new_base_ts);

 // Call this AFTER all the EndFrame/Flush/ResetTS stuff
 RebaseTS(v810_timestamp, new_base_ts);

 espec->MasterCycles = v810_timestamp - new_base_ts;

 V810_ResetTS(new_base_ts);
}

static void PCFX_Reset(void)
{
 const uint32 timestamp = v810_timestamp;

 //printf("Reset: %d\n", timestamp);

 // Make sure all devices are synched to current timestamp before calling their Reset()/Power()(though devices should already do this sort of thing on their
 // own, but it's not implemented for all of them yet, and even if it was all implemented this is also INSURANCE).
 ForceEventUpdates(timestamp);

 PCFX_Event_Reset();

 RAM_LPA = 0;

 ExBusReset = 0;
 BackupControl = 0;

 Last_VDC_AR[0] = 0;
 Last_VDC_AR[1] = 0;

 memset(RAM, 0x00, 2048 * 1024);

 for(uint_fast8_t i = 0; i < 2; i++)
 {
  int32 dummy_ne MDFN_NOWARN_UNUSED;

  dummy_ne = VDC_Reset(fx_vdc_chips[i]);
 }

 KING_Reset(timestamp);	// SCSICD_Power() is called from KING_Reset()
 SoundBox_Reset(timestamp);
 RAINBOW_Reset();
#ifdef HAVE_HUC6273
 if(WantHuC6273)
  HuC6273_Reset();
#endif
 PCFXIRQ_Reset();
 FXTIMER_Reset();
 V810_Reset();

 // Force device updates so we can get new next event timestamp values.
 ForceEventUpdates(timestamp);
}

static void PCFX_Power(void)
{
 PCFX_Reset();
}

static void VDCA_IRQHook(bool asserted)
{
 PCFXIRQ_Assert(PCFXIRQ_SOURCE_VDCA, asserted);
}

static void VDCB_IRQHook(bool asserted)
{
 PCFXIRQ_Assert(PCFXIRQ_SOURCE_VDCB, asserted);
}

static void PCFX_ClearPendingHuEXEUpload(void)
{
 PendingHuEXEUpload = false;
 PendingHuEXEUploadFrames = 0;
 PendingHuEXEStartPC = 0;
 free(PendingHuEXEImage);
 PendingHuEXEImage = NULL;
 PendingHuEXEImageSize = 0;
}

static void PCFX_MD5Hex(const uint8* data, size_t size, char out[33])
{
 uint8 digest[16];
 struct md5_context ctx;
 mednafen_md5_starts(&ctx);
 mednafen_md5_update(&ctx, (uint8*)data, (uint32_t)size);
 mednafen_md5_finish(&ctx, digest);
 for(int i = 0; i < 16; i++)
  sprintf(out + i * 2, "%02x", digest[i]);
 out[32] = 0;
}

static PCFXBIOSKind PCFX_ClassifyBIOS(const char* md5hex)
{
 if(!md5hex)
  return PCFX_BIOS_UNKNOWN;
 if(!strcasecmp(md5hex, "08e36edbea28a017f79f8d4f7ff9b6d7"))
  return PCFX_BIOS_CONSOLE_100;
 if(!strcasecmp(md5hex, "e2fb7c7220e3a7838c2dd7e401a7f3d8"))
  return PCFX_BIOS_CONSOLE_101;
 if(!strcasecmp(md5hex, "5885bc9a64bf80d4530b9b9b978ff587"))
  return PCFX_BIOS_FXGA;
 return PCFX_BIOS_UNKNOWN;
}



static bool PCFX_BIOSKindUsableForMode(PCFXBIOSKind kind, bool use_fxga)
{
 if(kind == PCFX_BIOS_UNKNOWN)
  return true;
 if(use_fxga)
  return kind == PCFX_BIOS_FXGA;
 return kind == PCFX_BIOS_CONSOLE_100 || kind == PCFX_BIOS_CONSOLE_101;
}

static struct MDFNFILE* PCFX_TryOpenBIOS(const char* path, PCFXBIOSKind* kind_out)
{
 struct MDFNFILE* fp = file_open(path);
 if(!fp)
  return NULL;
 if(fp->size != 1024 * 1024)
 {
  file_close(fp);
  return NULL;
 }

 char md5hex[33];
 PCFX_MD5Hex(fp->data, fp->size, md5hex);
 const PCFXBIOSKind kind = PCFX_ClassifyBIOS(md5hex);

 snprintf(CurrentBIOSPath, sizeof(CurrentBIOSPath), "%s", path);
 snprintf(CurrentBIOSMD5, sizeof(CurrentBIOSMD5), "%s", md5hex);
 CurrentBIOSKind = kind;
 if(kind_out)
  *kind_out = kind;
 return fp;
}


static bool PCFX_ReloadCurrentBIOSROM(void)
{
 if(!BIOSROM || !CurrentBIOSPath[0])
  return false;

 struct MDFNFILE* fp = file_open(CurrentBIOSPath);
 if(!fp)
  return false;

 if(fp->size != 1024 * 1024)
 {
  file_close(fp);
  return false;
 }

 PCFX_MD5Hex(fp->data, fp->size, CurrentBIOSMD5);
 CurrentBIOSKind = PCFX_ClassifyBIOS(CurrentBIOSMD5);
 memcpy(BIOSROM, fp->data, 1024 * 1024);
 file_close(fp);
 PCFX_ApplyBIOSPatchesToLoadedROM();
 return true;
}

static struct MDFNFILE* PCFX_TryOpenBIOSList(const char* base, const char* const* names, size_t count)
{
 for(size_t i = 0; i < count; i++)
 {
  char path[2048];
  snprintf(path, sizeof(path), "%s/%s", (base && base[0]) ? base : ".", names[i]);
  struct MDFNFILE* fp = PCFX_TryOpenBIOS(path, NULL);
  if(fp)
   return fp;
 }
 return NULL;
}

static struct MDFNFILE* PCFX_OpenBIOS(bool media_wants_fxga)
{
 static const char* console_names[] = {
  "pcfx.rom", "pcfxbios.bin", "pcfxv101.bin", "pcfx_bios.bin", "PCFX.ROM", "PCFXBIOS.BIN", "PCFXV101.BIN"
 };
 static const char* fxga_names[] = {
  "pcfxga.rom", "pcfxga.bin", "PCFXGA.ROM", "PCFXGA.BIN"
 };

 CurrentBIOSKind = PCFX_BIOS_UNKNOWN;
 CurrentBIOSPath[0] = 0;
 CurrentBIOSMD5[0] = 0;

 const bool force_fxga = PCFX_SystemModeForcesFXGA();
 const bool auto_mode = PCFX_SystemModeIsAuto();
 const bool media_prefers_fxga = ((EmuFlags & CDGE_FLAG_FXGA) || media_wants_fxga);
 const char* base = pcfx_base_directory[0] ? pcfx_base_directory : ".";
 struct MDFNFILE* fp = NULL;

 if(force_fxga)
 {
  fp = PCFX_TryOpenBIOSList(base, fxga_names, sizeof(fxga_names) / sizeof(fxga_names[0]));
  if(fp)
   return fp;
 }
 else if(auto_mode && media_prefers_fxga)
 {
  /* HuEXE and FXGA-flagged software often prefer the PC-FXGA BIOS, but they
   * should still boot with the standard PC-FX BIOS when no PC-FXGA dump is
   * available.  Only explicit PC-FXGA mode requires the PC-FXGA ROM. */
  fp = PCFX_TryOpenBIOSList(base, fxga_names, sizeof(fxga_names) / sizeof(fxga_names[0]));
  if(fp)
   return fp;
  fp = PCFX_TryOpenBIOSList(base, console_names, sizeof(console_names) / sizeof(console_names[0]));
  if(fp)
   return fp;
 }
 else
 {
  fp = PCFX_TryOpenBIOSList(base, console_names, sizeof(console_names) / sizeof(console_names[0]));
  if(fp)
   return fp;
  if(auto_mode)
  {
   fp = PCFX_TryOpenBIOSList(base, fxga_names, sizeof(fxga_names) / sizeof(fxga_names[0]));
   if(fp)
    return fp;
  }
 }

 PCFXBIOSKind base_kind = PCFX_BIOS_UNKNOWN;
 fp = PCFX_TryOpenBIOS(base, &base_kind);
 if(fp && !auto_mode && !PCFX_BIOSKindUsableForMode(base_kind, force_fxga))
 {
  file_close(fp);
  CurrentBIOSKind = PCFX_BIOS_UNKNOWN;
  CurrentBIOSPath[0] = 0;
  CurrentBIOSMD5[0] = 0;
  return NULL;
 }
 return fp;
}

static bool LoadCommon(CDIFList *CDInterfaces)
{
 struct MDFNFILE *BIOSFile = PCFX_OpenBIOS(LoadingHuEXE);
 if(!BIOSFile)
  return(0);

   #ifdef HAVE_HUC6273
   PCFX_ApplyHuC6273Presence();
   #endif

   V810_Init();
   V810_SetEmuTrapHandler(PCFX_PIOTrap);
   PCFX_PIOCloseAll();

   uint32 RAM_Map_Addresses[1] = { 0x00000000 };
   uint32 BIOSROM_Map_Addresses[1] = { 0xFFF00000 };

   RAM = V810_SetFastMap(RAM_Map_Addresses, 0x00200000, 1);

   // todo: cleanup on error
   if(!RAM)
      return(0);

   BIOSROM = V810_SetFastMap(BIOSROM_Map_Addresses, 0x00100000, 1);
   if(!BIOSROM)
      return(0);
   memcpy(BIOSROM, BIOSFile->data, 1024 * 1024);
   PCFX_ApplyBIOSPatchesToLoadedROM();

   file_close(BIOSFile);
   BIOSFile = NULL;

   for(uint_fast8_t i = 0; i < 2; i++)
   {
      fx_vdc_chips[i] = VDC_New(setting_nospritelimit, 65536);
      VDC_SetWSHook(fx_vdc_chips[i], NULL);
      VDC_SetIRQHook(fx_vdc_chips[i], i ? VDCB_IRQHook : VDCA_IRQHook);

      //fx_vdc_chips[0] = FXVDC_Init(PCFXIRQ_SOURCE_VDCA, MDFN_GetSettingB("pcfx.nospritelimit"));
      //fx_vdc_chips[1] = FXVDC_Init(PCFXIRQ_SOURCE_VDCB, MDFN_GetSettingB("pcfx.nospritelimit"));
   }

   SoundBox_Init();
   RAINBOW_Init(setting_rainbow_chromaip);
   FXINPUT_Init();
   FXTIMER_Init();

   #ifdef HAVE_HUC6273
   if(WantHuC6273)
      HuC6273_Init();
   #endif

   if(!KING_Init())
   {
      free(BIOSROM);
      free(RAM);
      BIOSROM = NULL;
      RAM = NULL;
      return(0);
   }

   CD_TrayOpen = false;
   CD_SelectedDisc = (CDInterfaces && CDInterfaces->count) ? 0 : -1;

   SCSICD_SetDisc(true, NULL, true);
   SCSICD_SetDisc(false, (CDInterfaces && CDInterfaces->count) ? CDInterfaces->items[0] : NULL, true);

   /*BRAMDisabled = 0;

   if(BRAMDisabled)
      MDFN_printf("Warning: BRAM is disabled per pcfx.disable_bram setting.  This is simulating a malfunction.\n");*/

   //if(!BRAMDisabled)
   {
      // Initialize Save RAM
      memset(SaveRAM, 0, sizeof(SaveRAM));

      static const uint8 BRInit00[] = { 0x24, 0x8A, 0xDF, 0x50, 0x43, 0x46, 0x58, 0x53, 0x72, 0x61, 0x6D, 0x80,
         0x00, 0x01, 0x01, 0x00, 0x01, 0x40, 0x00, 0x00, 0x01, 0xF9, 0x03, 0x00,
         0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
      };
      static const uint8 BRInit80[] = { 0xF9, 0xFF, 0xFF };

      memcpy(BackupRAM + 0x00, BRInit00, sizeof(BRInit00));
      memcpy(BackupRAM + 0x80, BRInit80, sizeof(BRInit80));


      static const uint8 ExBRInit00[] = { 0x24, 0x8A, 0xDF, 0x50, 0x43, 0x46, 0x58, 0x43, 0x61, 0x72, 0x64, 0x80,
         0x00, 0x01, 0x01, 0x00, 0x01, 0x40, 0x00, 0x00, 0x01, 0xF9, 0x03, 0x00,
         0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
      };
      static const uint8 ExBRInit80[] = { 0xF9, 0xFF, 0xFF };

      memcpy(ExBackupRAM + 0x00, ExBRInit00, sizeof(ExBRInit00));
      memcpy(ExBackupRAM + 0x80, ExBRInit80, sizeof(ExBRInit80));
   }

   // Default to 16-bit bus.
   for(uint_fast16_t i = 0; i < 256; i++)
   {
      V810_SetMemReadBus32(i, FALSE);
      V810_SetMemWriteBus32(i, FALSE);
   }

   // 16MiB RAM area.
   V810_SetMemReadBus32(0, TRUE);
   V810_SetMemWriteBus32(0, TRUE);

   // Bitstring read range
   for(uint_fast8_t i = 0xA0; i <= 0xAF; i++)
   {
      V810_SetMemReadBus32(i, FALSE);       // Reads to the read range are 16-bit, and
      V810_SetMemWriteBus32(i, TRUE);       // writes are 32-bit.
   }

   // Bitstring write range
   for(uint_fast8_t i = 0xB0; i <= 0xBF; i++)
   {
      V810_SetMemReadBus32(i, TRUE);	// Reads to the write range are 32-bit,
      V810_SetMemWriteBus32(i, FALSE);	// but writes are 16-bit!
   }

   // BIOS area
   for(uint_fast16_t i = 0xF0; i <= 0xFF; i++)
   {
      V810_SetMemReadBus32(i, FALSE);
      V810_SetMemWriteBus32(i, FALSE);
   }

   V810_SetMemReadHandlers(mem_rbyte, mem_rhword, mem_rword);
   V810_SetMemWriteHandlers(mem_wbyte, mem_whword, mem_wword);

   V810_SetIOReadHandlers(port_rbyte, port_rhword, NULL);
   V810_SetIOWriteHandlers(port_wbyte, port_whword, NULL);



   return(1);
}

static void DoMD5CDVoodoo(CDIFList *CDInterfaces)
{
 const CDGameEntry *found_entry = NULL;
 TOC toc;

 for(unsigned if_disc = 0; if_disc < CDInterfaces->count; if_disc++)
 {
  CDIF_ReadTOC_C(CDInterfaces->items[if_disc], &toc);

  if(toc.first_track == 1)
  {
   for(unsigned int g = 0; g < sizeof(GameList) / sizeof(CDGameEntry); g++)
   {
    const CDGameEntry *entry = &GameList[g];

    //assert(entry->discs == 1 || entry->discs == 2);

    for(unsigned int disc = 0; disc < entry->discs; disc++)
    {
     const CDGameEntryTrack *et = entry->tracks[disc];
     bool GameFound = TRUE;

     while(et->tracknum != -1 && GameFound)
     {
     // assert(et->tracknum > 0 && et->tracknum < 100);

      if(toc.tracks[et->tracknum].lba != et->lba)
       GameFound = FALSE;

      if( ((et->format == CDGE_FORMAT_DATA) ? 0x4 : 0x0) != (toc.tracks[et->tracknum].control & 0x4))
       GameFound = FALSE;

      et++;
     }

     if(et->tracknum == -1)
     {
      if((et - 1)->tracknum != toc.last_track)
       GameFound = FALSE;
 
      if(et->lba != toc.tracks[100].lba)
       GameFound = FALSE;
     }

     if(GameFound)
     {
      found_entry = entry;
      goto FoundIt;
     }
    } // End disc count loop
   }
  }

  FoundIt: ;

  if(found_entry)
  {
   EmuFlags = found_entry->flags;
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
   PCFXADPCMGameNeedsBuggyCodec = (found_entry->name && !strcmp(found_entry->name, "Miraculum: The Last Revelation"));
   PCFX_ApplyADPCMCompatOptions();
#endif

   if(found_entry->discs > 1)
   {
    const char *hash_prefix = "Mednafen PC-FX Multi-Game Set";
    struct md5_context md5_gameset;

    mednafen_md5_starts(&md5_gameset);

    mednafen_md5_update(&md5_gameset, (uint8_t*)hash_prefix, strlen(hash_prefix));

    for(unsigned int disc = 0; disc < found_entry->discs; disc++)
    {
     const CDGameEntryTrack *et = found_entry->tracks[disc];

     while(et->tracknum)
     {
      mednafen_md5_update_u32_as_lsb(&md5_gameset, et->tracknum);
      mednafen_md5_update_u32_as_lsb(&md5_gameset, (uint32)et->format);
      mednafen_md5_update_u32_as_lsb(&md5_gameset, et->lba);

      if(et->tracknum == -1)
       break;
      et++;
     }
    }
   }
   break;
  }
 } // end: for(unsigned if_disc = 0; if_disc < CDInterfaces->count; if_disc++)
}

static int LoadCD(CDIFList *CDInterfaces)
{
 EmuFlags = 0;
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
 PCFXADPCMGameNeedsBuggyCodec = false;
 PCFX_ApplyADPCMCompatOptions();
#endif

 cdifs = CDInterfaces;

 DoMD5CDVoodoo(CDInterfaces);

 if(!LoadCommon(CDInterfaces))
  return(0);

 //printf("Emulated CD-ROM drive speed: %ux\n", (unsigned int)MDFN_GetSettingUI("pcfx.cdspeed"));

 PCFX_Power();

 return(1);
}

/*
static void PCFX_CDInsertEject(void)
{
 CD_TrayOpen = !CD_TrayOpen;

 if(CD_TrayOpen)
  MDFN_DispMessage("Virtual CD Drive Tray Open");
 else
  MDFN_DispMessage("Virtual CD Drive Tray Closed");

 SCSICD_SetDisc(CD_TrayOpen, (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? cdifs->items[CD_SelectedDisc] : NULL);
}


static void PCFX_CDEject(void)
{
 if(!CD_TrayOpen)
  PCFX_CDInsertEject();
}

static void PCFX_CDSelect(void)
{
 if(cdifs && CD_TrayOpen)
 {
  CD_SelectedDisc = (CD_SelectedDisc + 1) % (cdifs->size() + 1);

  if((unsigned)CD_SelectedDisc == cdifs->size())
   CD_SelectedDisc = -1;

  if(CD_SelectedDisc == -1)
   MDFN_DispMessage("Disc absence selected.");
  else
   MDFN_DispMessage("Disc %d of %d selected.", CD_SelectedDisc + 1, (int)cdifs->size());
 }
}
*/

/*static void DoSimpleCommand(int cmd)
{
 switch(cmd)
 {
   case MDFN_MSC_INSERT_DISK:
		PCFX_CDInsertEject();
                break;

   case MDFN_MSC_SELECT_DISK:
		PCFX_CDSelect();
                break;

   case MDFN_MSC_EJECT_DISK:
		PCFX_CDEject();
                break;

  case MDFN_MSC_RESET: PCFX_Reset(); break;
  case MDFN_MSC_POWER: PCFX_Power(); break;
 }
}*/

int StateAction(StateMem *sm, int load, int data_only)
{
   const v810_timestamp_t timestamp = v810_timestamp;

   SFORMAT StateRegs[] =
   {
      SFARRAY(RAM, 0x200000),
      SFARRAY16(Last_VDC_AR, 2),
      SFVAR(BackupControl),
      SFVAR(ExBusReset),
      SFARRAY(BackupRAM, 0x8000),
      SFARRAY(ExBackupRAM, 0x8000),

      SFVAR(CD_TrayOpen),
      SFVAR(CD_SelectedDisc),

      SFEND
   };

   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "MAIN", false);

   for(uint_fast8_t i = 0; i < 2; i++)
      ret &= VDC_StateAction(fx_vdc_chips[i], sm, load, data_only, i ? "VDC1" : "VDC0");

   ret &= FXINPUT_StateAction(sm, load, data_only);
   ret &= PCFXIRQ_StateAction(sm, load, data_only);
   ret &= KING_StateAction(sm, load, data_only);
   ret &= V810_StateAction(sm, load, data_only);
   ret &= FXTIMER_StateAction(sm, load, data_only);
   ret &= SoundBox_StateAction(sm, load, data_only);
   ret &= SCSICD_StateAction(sm, load, data_only, "CDRM");
   ret &= RAINBOW_StateAction(sm, load, data_only);
   if(WantHuC6273)
      ret &= HuC6273_StateAction(sm, load, data_only);

   if(load)
   {
      //
      // Rather than bothering to store next event timestamp deltas in save states, we'll just recalculate next event times on save state load as a side effect
      // of this call.
      //
      ForceEventUpdates(timestamp);

      if(cdifs)
      {
         // Sanity check.
         if(CD_SelectedDisc >= (int)cdifs->count)
            CD_SelectedDisc = (int)cdifs->count - 1;

         SCSICD_SetDisc(CD_TrayOpen, (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? cdifs->items[CD_SelectedDisc] : NULL, true);
      }
   }

   //printf("0x%08x, %d %d %d %d\n", load, next_pad_ts, next_timer_ts, next_adpcm_ts, next_king_ts);

   return(ret);
}

#define MEDNAFEN_CORE_NAME_MODULE "pcfx"
#define MEDNAFEN_CORE_NAME "Beetle PC-FX"
#define MEDNAFEN_CORE_VERSION "v0.9.36.5"
#define MEDNAFEN_CORE_EXTENSIONS "cue|ccd|toc|chd|m3u|bin|iso|img|ex|exe"
#define MEDNAFEN_CORE_TIMING_FPS 59.94
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 256
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 240
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 256
#define FB_HEIGHT 240

#define FB_MAX_HEIGHT FB_HEIGHT

const char *mednafen_core_str = MEDNAFEN_CORE_NAME;

void Emu_Init(void)
{
   CDUtility_Init();
   snprintf(pcfx_base_directory, sizeof(pcfx_base_directory), "%s", home_path[0] ? home_path : ".");
   snprintf(pcfx_save_directory, sizeof(pcfx_save_directory), "%s", sram_path[0] ? sram_path : pcfx_base_directory);
}

/*void PCFX_Reset(void)
{
   DoSimpleCommand(MDFN_MSC_RESET);
}*/


static float mouse_sensitivity = 1.25f;

#define MAX_PLAYERS 2
#define MAX_BUTTONS 15
static uint16_t input_buf[MAX_PLAYERS] = {0};
static int32_t  mousedata[MAX_PLAYERS][3] = {{0}, {0}};

void PCFX_SetControllerType(uint8_t type)
{
 type = (type == 1) ? 1 : 0;
 option.type_controller = type;
 input_buf[0] = input_buf[1] = 0;
 memset(mousedata, 0, sizeof(mousedata));
 if(type == 1)
  FXINPUT_SetInput(0, 1, &mousedata[0]);
 else
  FXINPUT_SetInput(0, 0, &input_buf[0]);
 /* Port 2 remains a gamepad so two-player titles keep working even when
    port 1 is temporarily switched to the PC-FX mouse. */
 FXINPUT_SetInput(1, 0, &input_buf[1]);
}

uint8_t PCFX_GetControllerType(void)
{
 return option.type_controller == 1 ? 1 : 0;
}

typedef struct PathList
{
   char items[16][2048];
   unsigned count;
} PathList;

static void path_dirname_c(const char* path, char* out, size_t out_size)
{
   const char* slash = strrchr(path, '/');
#ifdef _WIN32
   const char* bslash = strrchr(path, '\\');
   if(!slash || (bslash && bslash > slash)) slash = bslash;
#endif
   if(!slash) snprintf(out, out_size, ".");
   else
   {
      size_t n = (size_t)(slash - path);
      if(n >= out_size) n = out_size - 1;
      memcpy(out, path, n);
      out[n] = 0;
   }
}

static void trim_whitespace_right_c(char* s)
{
   size_t n = strlen(s);
   while(n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static void eval_fip_c(const char* dir, const char* rel, char* out, size_t out_size)
{
   if(!rel || !rel[0]) { snprintf(out, out_size, "%s", dir ? dir : "."); return; }
   if(rel[0] == '/' || (strlen(rel) > 2 && rel[1] == ':')) snprintf(out, out_size, "%s", rel);
   else snprintf(out, out_size, "%s/%s", (dir && dir[0]) ? dir : ".", rel);
}

static bool ReadM3U(PathList* file_list, const char* path, unsigned depth)
{
   if(!file_list || !path || depth > 99) return false;
   FILE* fp = fopen(path, "rb");
   if(!fp) return false;
   char dir_path[2048];
   path_dirname_c(path, dir_path, sizeof(dir_path));
   char linebuf[2048];
   while(fgets(linebuf, sizeof(linebuf), fp))
   {
      trim_whitespace_right_c(linebuf);
      char* line = linebuf;
      while(*line && isspace((unsigned char)*line)) line++;
      if(line[0] == '#' || line[0] == 0) continue;
      char efp[2048];
      eval_fip_c(dir_path, line, efp, sizeof(efp));
      size_t len = strlen(efp);
      if(len >= 4 && !strcasecmp(efp + len - 4, ".m3u"))
      {
         if(!strcmp(efp, path)) { fclose(fp); return false; }
         if(!ReadM3U(file_list, efp, depth + 1)) { fclose(fp); return false; }
      }
      else if(file_list->count < 16)
      {
         snprintf(file_list->items[file_list->count++], sizeof(file_list->items[0]), "%s", efp);
      }
   }
   fclose(fp);
   return true;
}

static CDIFList CDInterfaces; /* FIXME: Cleanup on error out. */
// TODO: LoadCommon()



static char HuEXEBaseDir[1024];

typedef struct PCFXPIOFile
{
 FILE* fp;
} PCFXPIOFile;

enum { PCFX_PIO_MAX_FILES = 32 };
static PCFXPIOFile PCFXPIOFiles[PCFX_PIO_MAX_FILES];
static long PCFXPIOError;

enum
{
 PCFX_PIO_TRAP_SENDCHK  = 0x10,
 PCFX_PIO_TRAP_RECVCHK  = 0x11,
 PCFX_PIO_TRAP_FWRITE   = 0x12,
 PCFX_PIO_TRAP_FREAD    = 0x13,
 PCFX_PIO_TRAP_FSEEK    = 0x14,
 PCFX_PIO_TRAP_FOPEN    = 0x15,
 PCFX_PIO_TRAP_FCLOSE   = 0x16,
 PCFX_PIO_TRAP_PUTCHAR  = 0x17,
 PCFX_PIO_TRAP_PUTSTR   = 0x18,
 PCFX_PIO_TRAP_GETCHAR  = 0x19,
 PCFX_PIO_TRAP_GETERROR = 0x1A,
 PCFX_PIO_TRAP_ENTRY    = 0x1B
};

static bool PCFX_RAMPointerValid(uint32 addr, uint32 size)
{
 if(!RAM)
  return false;
 if(addr >= 0x200000)
  return false;
 return size <= 0x200000 - addr;
}

static uint8* PCFX_RAMPtr(uint32 addr, uint32 size)
{
 return PCFX_RAMPointerValid(addr, size) ? (RAM + addr) : NULL;
}

static void PCFX_ReadRAMString(uint32 addr, char* out, size_t out_size)
{
 if(!out_size)
  return;
 out[0] = 0;
 if(!PCFX_RAMPointerValid(addr, 1))
  return;
 size_t i = 0;
 while(i + 1 < out_size && PCFX_RAMPointerValid(addr + (uint32)i, 1))
 {
  const uint8 c = RAM[addr + (uint32)i];
  if(!c)
   break;
  out[i++] = (char)c;
 }
 out[i] = 0;
}

static void PCFX_PathDirname(const char* path, char* out, size_t out_size)
{
 if(!out_size)
  return;
 out[0] = 0;
 if(!path || !path[0])
  return;
 snprintf(out, out_size, "%s", path);
 char* slash1 = strrchr(out, '/');
 char* slash2 = strrchr(out, '\\');
 char* slash = slash1 > slash2 ? slash1 : slash2;
 if(slash)
 {
  if(slash == out)
   slash[1] = 0;
  else
   *slash = 0;
 }
 else
  snprintf(out, out_size, ".");
}

static void PCFX_PIOCloseAll(void)
{
 for(unsigned i = 0; i < PCFX_PIO_MAX_FILES; i++)
 {
  if(PCFXPIOFiles[i].fp)
  {
   fclose(PCFXPIOFiles[i].fp);
   PCFXPIOFiles[i].fp = NULL;
  }
 }
 PCFXPIOError = 0;
}

static int PCFX_PIOAllocHandle(FILE* fp)
{
 if(!fp)
  return -1;
 for(unsigned i = 1; i < PCFX_PIO_MAX_FILES; i++)
 {
  if(!PCFXPIOFiles[i].fp)
  {
   PCFXPIOFiles[i].fp = fp;
   return (int)i;
  }
 }
 fclose(fp);
 PCFXPIOError = -EMFILE;
 return -1;
}

static FILE* PCFX_PIOFileFromHandle(uint32 handle)
{
 if(handle >= PCFX_PIO_MAX_FILES || !PCFXPIOFiles[handle].fp)
 {
  PCFXPIOError = -EBADF;
  return NULL;
 }
 return PCFXPIOFiles[handle].fp;
}

static FILE* PCFX_PIOOpenHostFile(const char* file, const char* mode)
{
 if(!file || !file[0])
  return NULL;
 if(!mode || !mode[0])
  mode = "rb";

 FILE* fp = fopen(file, mode);
 if(fp)
  return fp;

 if(HuEXEBaseDir[0] && file[0] != '/' && !(isalpha((unsigned char)file[0]) && file[1] == ':'))
 {
  char joined[2048];
  snprintf(joined, sizeof(joined), "%s/%s", HuEXEBaseDir, file);
  fp = fopen(joined, mode);
  if(fp)
   return fp;
 }
 return NULL;
}

static void PCFX_PIOTrapReturn(uint32 value)
{
 V810_SetPR(10, value);
 V810_SetPC(V810_GetPR(31) & ~1U);
}

static bool PCFX_PIOTrap(uint32 trap_id)
{
 if(trap_id < PCFX_PIO_TRAP_SENDCHK || trap_id > PCFX_PIO_TRAP_ENTRY)
  return false;

 uint32 ret = 0;
 switch(trap_id)
 {
  case PCFX_PIO_TRAP_SENDCHK:
   ret = 1;
   break;
  case PCFX_PIO_TRAP_RECVCHK:
   ret = 0;
   break;
  case PCFX_PIO_TRAP_FOPEN:
  {
   char file[1024], mode[32];
   PCFX_ReadRAMString(V810_GetPR(6), file, sizeof(file));
   PCFX_ReadRAMString(V810_GetPR(7), mode, sizeof(mode));
   FILE* fp = PCFX_PIOOpenHostFile(file, mode[0] ? mode : "rb");
   if(!fp)
   {
    PCFXPIOError = -ENOENT;
    ret = (uint32)-1;
   }
   else
    ret = (uint32)PCFX_PIOAllocHandle(fp);
   break;
  }
  case PCFX_PIO_TRAP_FREAD:
  {
   FILE* fp = PCFX_PIOFileFromHandle(V810_GetPR(6));
   const uint32 len = V810_GetPR(7);
   uint8* dst = PCFX_RAMPtr(V810_GetPR(8), len ? len : 1);
   if(!fp || !dst || !len)
   {
    ret = (uint32)-1;
    if(!dst) PCFXPIOError = -EFAULT;
   }
   else
   {
    size_t got = fread(dst, 1, len, fp);
    if(!got && ferror(fp)) { PCFXPIOError = -EIO; ret = (uint32)-1; }
    else ret = (uint32)got;
   }
   break;
  }
  case PCFX_PIO_TRAP_FWRITE:
  {
   FILE* fp = PCFX_PIOFileFromHandle(V810_GetPR(6));
   const uint32 len = V810_GetPR(7);
   uint8* src = PCFX_RAMPtr(V810_GetPR(8), len ? len : 1);
   if(!fp || !src || !len)
   {
    ret = (uint32)-1;
    if(!src) PCFXPIOError = -EFAULT;
   }
   else
   {
    size_t put = fwrite(src, 1, len, fp);
    if(!put && ferror(fp)) { PCFXPIOError = -EIO; ret = (uint32)-1; }
    else ret = (uint32)put;
   }
   break;
  }
  case PCFX_PIO_TRAP_FSEEK:
  {
   FILE* fp = PCFX_PIOFileFromHandle(V810_GetPR(6));
   const long pos = (long)(int32)V810_GetPR(7);
   const int whence = (int)V810_GetPR(8);
   int host_whence = SEEK_SET;
   if(whence == 1) host_whence = SEEK_CUR;
   else if(whence == 2) host_whence = SEEK_END;
   if(!fp || fseek(fp, pos, host_whence) != 0)
   {
    PCFXPIOError = -EIO;
    ret = (uint32)-1;
   }
   else
   {
    long p = ftell(fp);
    ret = (p < 0) ? 0 : (uint32)p;
   }
   break;
  }
  case PCFX_PIO_TRAP_FCLOSE:
  {
   const uint32 handle = V810_GetPR(6);
   if(handle < PCFX_PIO_MAX_FILES && PCFXPIOFiles[handle].fp)
   {
    fclose(PCFXPIOFiles[handle].fp);
    PCFXPIOFiles[handle].fp = NULL;
   }
   ret = 0;
   break;
  }
  case PCFX_PIO_TRAP_PUTCHAR:
   ret = 0;
   break;
  case PCFX_PIO_TRAP_PUTSTR:
   // Keep PIO debug output quiet in normal frontends.  File I/O side effects
   // are what GMAKER samples require; text output is optional.
   ret = 0;
   break;
  case PCFX_PIO_TRAP_GETCHAR:
   ret = (uint32)-1;
   break;
  case PCFX_PIO_TRAP_GETERROR:
   ret = (uint32)PCFXPIOError;
   break;
  case PCFX_PIO_TRAP_ENTRY:
   ret = 0;
   break;
 }

 PCFX_PIOTrapReturn(ret);
 return true;
}

static uint32 HuEXE_ReadBE32(const uint8* p)
{
 return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | (uint32)p[3];
}

static bool HuEXE_FindPublicSymbol(const uint8* data, size_t size, const char* name, uint32* address)
{
 if(!data || size < 0x40 || !name || !address)
  return false;

 const uint32 sym_off = HuEXE_ReadBE32(data + 0x14);
 const uint32 sym_size = HuEXE_ReadBE32(data + 0x18);
 const uint32 str_size = HuEXE_ReadBE32(data + 0x1C);
 const uint32 str_off = sym_off + sym_size;

 if(sym_off >= size || sym_size > size - sym_off || str_off >= size || str_size > size - str_off)
  return false;

 uint32 index = 0;
 uint32 pos = str_off;
 const uint32 str_end = str_off + str_size;
 while(pos < str_end)
 {
  const char* s = (const char*)&data[pos];
  uint32 len = 0;
  while(pos + len < str_end && data[pos + len])
   len++;

  if(len && !strcmp(s, name) && (uint64)index * 4 + 4 <= sym_size)
  {
   const uint32 rec = HuEXE_ReadBE32(data + sym_off + index * 4);
   *address = rec & 0x00FFFFFF;
   return true;
  }

  pos += len + 1;
  index++;
 }
 return false;
}

static bool HuEXE_IsPIOStubSymbol(const char* name)
{
 if(!name)
  return false;
 return !strcmp(name, "pio_sendchk") || !strcmp(name, "pio_recvchk") ||
        !strcmp(name, "pio_fwrite")  || !strcmp(name, "pio_fread") ||
        !strcmp(name, "pio_fseek")   || !strcmp(name, "pio_fopen") ||
        !strcmp(name, "pio_fclose")  || !strcmp(name, "pio_putchar") ||
        !strcmp(name, "pio_putstr")  || !strcmp(name, "pio_getchar") ||
        !strcmp(name, "pio_geterror") || !strcmp(name, "___pio_entry") ||
        !strcmp(name, "__pio_entry");
}

static uint8 HuEXE_PIOTrapIDForSymbol(const char* name)
{
 if(!name) return 0;
 if(!strcmp(name, "pio_sendchk"))  return PCFX_PIO_TRAP_SENDCHK;
 if(!strcmp(name, "pio_recvchk"))  return PCFX_PIO_TRAP_RECVCHK;
 if(!strcmp(name, "pio_fwrite"))   return PCFX_PIO_TRAP_FWRITE;
 if(!strcmp(name, "pio_fread"))    return PCFX_PIO_TRAP_FREAD;
 if(!strcmp(name, "pio_fseek"))    return PCFX_PIO_TRAP_FSEEK;
 if(!strcmp(name, "pio_fopen"))    return PCFX_PIO_TRAP_FOPEN;
 if(!strcmp(name, "pio_fclose"))   return PCFX_PIO_TRAP_FCLOSE;
 if(!strcmp(name, "pio_putchar"))  return PCFX_PIO_TRAP_PUTCHAR;
 if(!strcmp(name, "pio_putstr"))   return PCFX_PIO_TRAP_PUTSTR;
 if(!strcmp(name, "pio_getchar"))  return PCFX_PIO_TRAP_GETCHAR;
 if(!strcmp(name, "pio_geterror")) return PCFX_PIO_TRAP_GETERROR;
 if(!strcmp(name, "___pio_entry") || !strcmp(name, "__pio_entry")) return PCFX_PIO_TRAP_ENTRY;
 return 0;
}

static void HuEXE_WriteLE16ToRAM(uint32 address, uint16 value)
{
 if(address + 1 >= 0x200000)
  return;
 RAM[address + 0] = (uint8)(value & 0xFF);
 RAM[address + 1] = (uint8)(value >> 8);
}

static void HuEXE_WritePIOTrapStub(uint32 address, uint8 trap_id)
{
 if(address + 3 >= 0x200000)
  return;
 // V810 TRAP immediate encoding is 0x6000 | imm5.  The emulator trap handler
 // services GMAKER/PIOLIB calls and returns through r31 after placing the
 // scalar result in r10.  The jmp [r31] fallback is left in place for safety if
 // a debugger advances past the trap manually.
 HuEXE_WriteLE16ToRAM(address + 0, (uint16)(0x6000 | (trap_id & 0x1F)));
 HuEXE_WriteLE16ToRAM(address + 2, 0x181F);
}

static void HuEXE_PatchPIOStubs(const uint8* data, size_t size)
{
 if(!data || size < 0x40)
  return;

 const uint32 sym_off = HuEXE_ReadBE32(data + 0x14);
 const uint32 sym_size = HuEXE_ReadBE32(data + 0x18);
 const uint32 str_size = HuEXE_ReadBE32(data + 0x1C);
 const uint32 str_off = sym_off + sym_size;

 if(sym_off >= size || sym_size > size - sym_off || str_off >= size || str_size > size - str_off)
  return;

 uint32 index = 0;
 uint32 pos = str_off;
 const uint32 str_end = str_off + str_size;
 while(pos < str_end)
 {
  const char* name = (const char*)&data[pos];
  uint32 len = 0;
  while(pos + len < str_end && data[pos + len])
   len++;

  if(len && HuEXE_IsPIOStubSymbol(name) && (uint64)index * 4 + 4 <= sym_size)
  {
   const uint32 rec = HuEXE_ReadBE32(data + sym_off + index * 4);
   const uint32 address = rec & 0x00FFFFFF;
   const uint8 trap_id = HuEXE_PIOTrapIDForSymbol(name);
   if(address < 0x200000 && trap_id)
    HuEXE_WritePIOTrapStub(address, trap_id);
  }

  pos += len + 1;
  index++;
 }
}

static bool HuEXE_LoadSegments(const uint8* data, size_t size, uint32* start_pc)
{
 if(!data || size < 0x40 || memcmp(data, "HuEXE001", 8))
  return false;

 const uint32 segment_count = HuEXE_ReadBE32(data + 0x0C);
 const uint32 segment_table = 0x40;

 if(segment_count > 256 || segment_table + (uint64)segment_count * 0x30 > size)
  return false;

 for(uint32 i = 0; i < segment_count; i++)
 {
  const uint8* sh = data + segment_table + i * 0x30;
  const uint32 file_off = HuEXE_ReadBE32(sh + 0x10);
  const uint32 mem_size = HuEXE_ReadBE32(sh + 0x14);
  const uint32 load_addr = HuEXE_ReadBE32(sh + 0x24);

  if(!mem_size)
   continue;
  if(load_addr >= 0x200000 || mem_size > 0x200000 - load_addr)
   return false;

  if(file_off && file_off < size)
  {
   const uint32 copy_size = (mem_size <= size - file_off) ? mem_size : (uint32)(size - file_off);
   memcpy(RAM + load_addr, data + file_off, copy_size);
   if(copy_size < mem_size)
    memset(RAM + load_addr + copy_size, 0, mem_size - copy_size);
  }
  else
  {
   memset(RAM + load_addr, 0, mem_size);
  }
 }

 HuEXE_PatchPIOStubs(data, size);

 uint32 pc = 0;
 if(HuEXE_FindPublicSymbol(data, size, "__start", &pc) || HuEXE_FindPublicSymbol(data, size, "main", &pc))
 {
  if(pc < 0x200000)
  {
   *start_pc = pc;
   return true;
  }
 }

 // GMAKER's documented default link address is 0x8000 when -P8000 is used.
 *start_pc = 0x8000;
 return true;
}

static int LoadHuEXE(const char* name)
{
 PCFX_PathDirname(name, HuEXEBaseDir, sizeof(HuEXEBaseDir));
 EmuFlags = CDGE_FLAG_FXGA;
#ifdef HAVE_HUC6273
 PCFX_ApplyHuC6273Presence();
#endif
 cdifs = NULL;
 CD_TrayOpen = false;
 CD_SelectedDisc = -1;
 PCFX_ClearPendingHuEXEUpload();

 LoadingHuEXE = true;
 const bool common_loaded = LoadCommon(NULL);
 LoadingHuEXE = false;
 if(!common_loaded)
  return 0;

 struct MDFNFILE* EXEFile = file_open(name);
 if(!EXEFile)
  return 0;

 PCFX_Power();

 uint32 start_pc = 0;
 if(CurrentBIOSKind == PCFX_BIOS_FXGA)
 {
  const uint8* exe_data = EXEFile->data;
  const size_t exe_size = EXEFile->size;
  if(!HuEXE_LoadSegments(exe_data, exe_size, &start_pc))
  {
   file_close(EXEFile);
   return 0;
  }
  memset(RAM, 0x00, 2048 * 1024);
  PendingHuEXEImage = (uint8*)malloc(exe_size);
  if(!PendingHuEXEImage) { file_close(EXEFile); return 0; }
  memcpy(PendingHuEXEImage, exe_data, exe_size);
  PendingHuEXEImageSize = exe_size;
  PendingHuEXEStartPC = start_pc;
  PendingHuEXEUploadFrames = PCFXGA_HUEXE_UPLOAD_DELAY_FRAMES;
  PendingHuEXEUpload = true;
  file_close(EXEFile);
  ForceEventUpdates(v810_timestamp);
  return 1;
 }

 const bool loaded = HuEXE_LoadSegments(EXEFile->data, EXEFile->size, &start_pc);
 file_close(EXEFile);
 if(!loaded)
  return 0;

 V810_SetPC(start_pc & ~1U);
 V810_SetPR(3, 0x00000E00);
 ForceEventUpdates(v810_timestamp);
 return 1;
}

static int OpenCDList(const char *devicename, CDIFList *out)
{
 if(!devicename || !out)
  return 0;

 memset(out, 0, sizeof(*out));

 if(strlen(devicename) > 4 && !strcasecmp(devicename + strlen(devicename) - 4, ".m3u"))
 {
  PathList file_list;
  memset(&file_list, 0, sizeof(file_list));
  if(!ReadM3U(&file_list, devicename, 0))
   return 0;
  for(unsigned i = 0; i < file_list.count; i++)
  {
   CDIF* cdif = CDIF_Open_C(file_list.items[i], false);
   if(!cdif || !CDIFList_Push(out, cdif))
   {
    if(cdif) CDIF_Close_C(cdif);
    CDIFList_Clear(out);
    return 0;
   }
  }
 }
 else
 {
  CDIF* cdif = CDIF_Open_C(devicename, false);
  if(!cdif || !CDIFList_Push(out, cdif))
  {
   if(cdif) CDIF_Close_C(cdif);
   return 0;
  }
 }
 return out->count ? 1 : 0;
}

uint8_t MDFNI_LoadCD(const char *devicename)
{
 HuEXEBaseDir[0] = 0;
 uint8 LayoutMD5[16];
 (void)LayoutMD5;
 CDIFList_Clear(&CDInterfaces);

 if(!OpenCDList(devicename, &CDInterfaces))
  return 0;

 struct md5_context layout_md5;
 mednafen_md5_starts(&layout_md5);
 for(unsigned i = 0; i < CDInterfaces.count; i++)
 {
  TOC toc;
  CDIF_ReadTOC_C(CDInterfaces.items[i], &toc);
  mednafen_md5_update_u32_as_lsb(&layout_md5, toc.first_track);
  mednafen_md5_update_u32_as_lsb(&layout_md5, toc.last_track);
  mednafen_md5_update_u32_as_lsb(&layout_md5, toc.tracks[100].lba);
  for(uint32 track = toc.first_track; track <= toc.last_track; track++)
  {
   mednafen_md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].lba);
   mednafen_md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].control & 0x4);
  }
 }
 mednafen_md5_finish(&layout_md5, LayoutMD5);

 if(!(LoadCD(&CDInterfaces)))
 {
  CDIFList_Clear(&CDInterfaces);
  return 0;
 }
 return 1;
}

int PCFX_SwapCD(const char *path)
{
 if(!path || !path[0] || !cdifs)
  return 0;

 CDIFList new_list;
 if(!OpenCDList(path, &new_list))
  return 0;

 /* Open the new image before disturbing the current drive state.  Once the
  * image is known-good, emulate an eject, close the old image(s), install the
  * new list, and close the tray.  SCSICD_SetDisc(..., false) on tray close
  * raises UNIT ATTENTION / disc-changed state for software that polls the
  * drive after a multi-disc prompt. */
 SCSICD_SetDisc(true, NULL, false);
 CDIFList_Clear(cdifs);
 *cdifs = new_list;
 CD_SelectedDisc = cdifs->count ? 0 : -1;
 CD_TrayOpen = false;
 SCSICD_SetDisc(false, CD_SelectedDisc >= 0 ? cdifs->items[CD_SelectedDisc] : NULL, false);
 return CD_SelectedDisc >= 0;
}

static uint8_t MDFNI_LoadBIOSOnly(void)
{
 HuEXEBaseDir[0] = 0;
 EmuFlags = PCFX_SystemModeForcesFXGA() ? CDGE_FLAG_FXGA : 0;
#ifdef HAVE_HUC6273
 PCFX_ApplyHuC6273Presence();
#endif
 cdifs = NULL;
 CD_TrayOpen = false;
 CD_SelectedDisc = -1;
 PCFX_ClearPendingHuEXEUpload();
 LoadingHuEXE = false;

 if(!LoadCommon(NULL))
  return 0;

 PCFX_Power();
 return 1;
}

static uint8_t MDFNI_LoadGame(const char *name)
{
   if(!name || !name[0])
      return MDFNI_LoadBIOSOnly();
   const size_t len = strlen(name);
   if(CDIF_IsPhysicalPath_C(name))
      return (MDFNI_LoadCD(name));
   if(len > 4 && (!strcasecmp(name + len - 4, ".cue") || !strcasecmp(name + len - 4, ".ccd") ||
#ifdef HAVE_CHD
   !strcasecmp(name + len - 4, ".chd") ||
#endif
   !strcasecmp(name + len - 4, ".toc") || !strcasecmp(name + len - 4, ".m3u") ||
   !strcasecmp(name + len - 4, ".bin") || !strcasecmp(name + len - 4, ".iso") || !strcasecmp(name + len - 4, ".img")))
   {
      return (MDFNI_LoadCD(name));
   }
   if((len > 3 && !strcasecmp(name + len - 3, ".ex")) || (len > 4 && !strcasecmp(name + len - 4, ".exe")))
      return LoadHuEXE(name);
   return 0;
}

static int FinishLoadMemory(void)
{
	PCFX_SetControllerType(option.type_controller);
	input_buf[1] = 0;
	KING_SetPixelFormat();
	SoundBox_SetSoundRate(SOUND_OUTPUT_FREQUENCY);
	return 1;
}

int Load_Game_Memory(char* path)
{
	const int loaded = MDFNI_LoadGame(path);
	if(!loaded)
		return 0;
	return FinishLoadMemory();
}

int Load_BIOS_Memory(void)
{
	if(!MDFNI_LoadBIOSOnly())
		return 0;
	return FinishLoadMemory();
}

void PCFX_SoftReset(void)
{
	/* Reload the original BIOS image before reset so changed in-memory BIOS
	 * patch options are applied and previously applied patches are removed
	 * cleanly when their option is disabled. */
	PCFX_ReloadCurrentBIOSROM();
	PCFX_Reset();
}

static void update_input(void)
{
	Read_General_Input();
	switch(option.type_controller)
	{
		default:
			input_buf[0] = Read_Pad_Input_Player(0);
			input_buf[1] = Read_Pad_Input_Player(1);
			mousedata[0][0] = 0;
			mousedata[0][1] = 0;
			mousedata[0][2] = 0;
		break;
		case 1:
			input_buf[0] = 0;
			input_buf[1] = Read_Pad_Input_Player(1);
			mousedata[0][0] = (int)roundf( (float)Read_Mouse_X() * mouse_sensitivity);
			mousedata[0][1] = (int)roundf( (float)Read_Mouse_Y() * mouse_sensitivity);
			mousedata[0][2] = Read_Mouse_buttons();
		break;
	}
}

static uint64_t video_frames, audio_frames;

#if defined(FRAMESKIP) || defined(FORCE_FRAMESKIP)

#ifndef FORCE_FRAMESKIP
static uint32_t Timer_Read(void) 
{
	/* Timing. */
	struct timeval tval;
  	gettimeofday(&tval, 0);
	return (((tval.tv_sec*1000000) + (tval.tv_usec)));
}
static long lastTick = 0, newTick;
static uint32_t FPS = MEDNAFEN_CORE_TIMING_FPS;
#endif
static uint32_t SkipCnt = 0;
static uint32_t FrameSkip;
static const uint32_t TblSkip[5][5] = {
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1},
    {0, 0, 0, 1, 1},
    {0, 0, 1, 1, 1},
    {0, 1, 1, 1, 1},
};
#endif


static void PCFX_ServicePendingHuEXEUpload(void)
{
 if(!PendingHuEXEUpload)
  return;
 if(PendingHuEXEUploadFrames)
 {
  PendingHuEXEUploadFrames--;
  return;
 }

 uint32 start_pc = PendingHuEXEStartPC;
 if(PendingHuEXEImage && PendingHuEXEImageSize && HuEXE_LoadSegments(PendingHuEXEImage, PendingHuEXEImageSize, &start_pc))
 {
  V810_SetPC(start_pc & ~1U);
  V810_SetPR(3, 0x00000E00);
  ForceEventUpdates(v810_timestamp);
 }
 PCFX_ClearPendingHuEXEUpload();
}

void Emulation_Run()
{
#ifdef PCFX_HEADLESS
   pcfx_headless_video_set_full_width(WantHuC6273 ? 1 : 0);
#endif
#ifdef PCFX_WIN32
   PCFX_Win32_SetFullWidth(WantHuC6273 ? 1 : 0);
#endif
   PCFX_ServicePendingHuEXEUpload();
	EmulateSpecStruct spec = {0};
	static int16_t sound_buf[0x10000];
	static int32 rects[FB_MAX_HEIGHT];
	rects[0] = ~0;

	update_input();

	spec.SoundRate = SOUND_OUTPUT_FREQUENCY;
	spec.SoundBuf = sound_buf;
	spec.LineWidths = rects;
	spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
	spec.SoundVolume = 1.0;
	spec.soundmultiplier = 1.0;
	spec.SoundBufSize = 0;
#if defined(FRAMESKIP) || defined(FORCE_FRAMESKIP)
#ifdef FORCE_FRAMESKIP
	FrameSkip = 4;
#endif
	SkipCnt++;
	if (SkipCnt > 4) SkipCnt = 0;
	spec.skip = TblSkip[FrameSkip][SkipCnt];
#else
	spec.skip = 0;
#endif
	Emulate(&spec);
#ifdef PCFX_HEADLESS
   pcfx_headless_video_set_display_width(spec.DisplayRect.w);
#endif
#ifdef PCFX_WIN32
   PCFX_Win32_SetDisplayWidth(spec.DisplayRect.w);
#endif

   //int16 *const SoundBuf = spec.SoundBuf + spec.SoundBufSizeALMS * curgame->soundchan;
   int32 SoundBufSize = spec.SoundBufSize - spec.SoundBufSizeALMS;
   //const int32 SoundBufMaxSize = spec.SoundBufMaxSize - spec.SoundBufSizeALMS;

   spec.SoundBufSize = spec.SoundBufSizeALMS + SoundBufSize;
   Update_Video_Ingame();

   audio_frames += spec.SoundBufSize;
   
#ifdef FRAMESKIP
	newTick = Timer_Read();
	if ( (newTick) - (lastTick) > 1000000) 
	{
		FPS = video_frames;
		video_frames = 0;
		lastTick = newTick;
		if (FPS >= 60)
		{
			FrameSkip = 0;
		}
		else
		{
			if (FPS > 55) FrameSkip = 2;
			else if (FPS > 45) FrameSkip = 3;
			else FrameSkip = 4;
		}
	}
	if (spec.skip == false) video_frames++;
#elif !defined(FORCE_FRAMESKIP)
	video_frames++;
#endif

	Audio_Write((int16_t*) spec.SoundBuf, spec.SoundBufSize);
}

static size_t serialize_size;

size_t PCFX_StateSerializeSize(void)
{
   StateMem st;

   st.data           = NULL;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = 0;
   st.initial_malloc = 0;

   if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL))
   {
      free(st.data);
      serialize_size = 0;
      return 0;
   }

   serialize_size = st.len;
   free(st.data);
   return serialize_size;
}

bool PCFX_StateSerialize(void *data, size_t size)
{
   if(!data || !size)
      return false;

   StateMem st;
   st.data           = NULL;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = 0;
   st.initial_malloc = (uint32_t)(size > 0xFFFFFFFFu ? 0xFFFFFFFFu : size);

   const bool ret = MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL) ? true : false;
   if(!ret || st.len > size)
   {
      free(st.data);
      return false;
   }

   memcpy(data, st.data, st.len);
   if(st.len < size)
      memset((uint8_t*)data + st.len, 0, size - st.len);
   serialize_size = st.len;
   free(st.data);
   return true;
}

size_t PCFX_StateLastSerializeSize(void)
{
   return serialize_size;
}

bool PCFX_StateSerializeMalloc(uint8_t **out_data, size_t *out_size)
{
   if(!out_data || !out_size)
      return false;
   *out_data = NULL;
   *out_size = 0;

   StateMem st;
   st.data           = NULL;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = 0;
   st.initial_malloc = (uint32_t)(serialize_size ? serialize_size : 32768);

   if(!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL) || !st.data || !st.len)
   {
      free(st.data);
      return false;
   }

   *out_data = st.data;
   *out_size = st.len;
   serialize_size = st.len;
   return true;
}

bool PCFX_StateUnserialize(const void *data, size_t size)
{
   if(!data || size < 32)
      return false;

   StateMem st;

   st.data           = (uint8_t*)data;
   st.loc            = 0;
   st.len            = size;
   st.malloced       = 0;
   st.initial_malloc = 0;

   return MDFNSS_LoadSM(&st, 0, 0) ? true : false;
}

/* MDFN_MakeFName C++ string helper removed in C11 core path. */

bool SaveState(char* path, uint_fast8_t state)
{
   FILE* savefp;
   long file_size;
   char* buffer = NULL;
   bool ok = false;

   if(!path || !path[0])
      return false;

   if(state == 1)
   {
      savefp = fopen(path, "rb");
      if(!savefp)
         return false;
      if(fseek(savefp, 0, SEEK_END) || (file_size = ftell(savefp)) <= 0 || fseek(savefp, 0, SEEK_SET))
      {
         fclose(savefp);
         return false;
      }
      buffer = (char*)malloc((size_t)file_size);
      if(buffer && fread(buffer, 1, (size_t)file_size, savefp) == (size_t)file_size)
         ok = PCFX_StateUnserialize(buffer, (size_t)file_size);
      fclose(savefp);
   }
   else
   {
      uint8_t* state_data = NULL;
      size_t actual = 0;
      if(!PCFX_StateSerializeMalloc(&state_data, &actual))
         return false;
      buffer = (char*)state_data;
      savefp = fopen(path, "wb");
      if(!savefp)
         goto out;
      ok = fwrite(buffer, 1, actual, savefp) == actual;
      if(fclose(savefp))
         ok = false;
   }

out:
   free(buffer);
   return ok;
}


void SRAM_Save(char* path, uint_fast8_t state)
{	
	FILE* savefp;
	size_t file_size;
	if (state == 1)
	{
		savefp = fopen(path, "rb");
		if (savefp)
		{
			fseek(savefp, 0, SEEK_END);
			file_size = ftell(savefp);
			fseek(savefp, 0, SEEK_SET);
			fread((uint8_t*)SaveRAM, sizeof(uint8_t), file_size, savefp);
			fclose(savefp);
		}
	}
	else
	{
		file_size = sizeof(SaveRAM);
		if (file_size > 0)
		{
			savefp = fopen(path, "wb");
			if (savefp)
			{
				fwrite((uint8_t*)SaveRAM, sizeof(uint8_t), file_size, savefp);
				fclose(savefp);
			}
		}
	}
}

static void Clean_Emu(void)
{
   uint_fast8_t i;

   for(i = 0; i < 2; i++)
   {
      if(fx_vdc_chips[i])
      {
         VDC_Delete(fx_vdc_chips[i]);
         fx_vdc_chips[i] = NULL;
      }
   }

   FXINPUT_Kill();
#ifdef HAVE_HUC6273
   WantHuC6273 = false;
#endif
   SCSICD_SetDisc(true, NULL, true);
   CDIFList_Clear(&CDInterfaces);
   cdifs = NULL;
   CD_TrayOpen = false;
   CD_SelectedDisc = -1;
   RAINBOW_Close();
   KING_Close();
   SoundBox_Kill();
   V810_Kill();

   // The allocated memory RAM and BIOSROM is free'd in V810_Kill()
   RAM = NULL;
   BIOSROM = NULL;
}

void PCFX_CoreClose(void)
{
   Clean_Emu();
}

#ifdef PCFX_HEADLESS
void PCFX_Headless_CoreClose(void)
{
   PCFX_CoreClose();
}

uint8_t* PCFX_Headless_CoreRAM(size_t* size)
{
   if(size) *size = 2048 * 1024;
   return RAM;
}

uint8_t* PCFX_Headless_CoreSaveRAM(size_t* size)
{
   if(size) *size = sizeof(SaveRAM);
   return SaveRAM;
}
#endif

#if !defined(PCFX_HEADLESS) && !defined(PCFX_EXTERNAL_FRONTEND)
/* Main entrypoint of the emulator */
int main(int argc, char* argv[])
{
	printf("Starting PCFXEmu\n");
	if (argc < 2)
	{
		printf("Specify a ROM to load in memory\n");
		return 0;
	}
	
	snprintf(GameName_emu, sizeof(GameName_emu), "%s", basename(argv[1]));

	Init_Configuration();
	
	Emu_Init();
	Load_Game_Memory(argv[1]);
	
	Init_Video();
	Audio_Init();	
	Load_Configuration();
	
    // get the game ready
    while (!exit_vb)
    {
		switch(emulator_state)
		{
			case 0:
				Emulation_Run();
			break;
			case 1:
				Menu();
			break;
		}
    }
    
	Clean_Emu();
	Clean();
    Audio_Close();
    Video_Close();

    return 0;
}
#endif
