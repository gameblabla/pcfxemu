#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <libgen.h>
#define _BSD_SOURCE
#include <sys/time.h>

#ifdef _MSC_VER
#include <compat/msvc.h>
#endif

#include <string/stdstring.h>
#include <retro_timers.h>
#include <streams/file_stream.h>

#include "mednafen/mednafen.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include "mednafen/md5.h"
#include	"FileWrapper.h"
#ifdef NEED_DEINTERLACER
#include	"mednafen/video/Deinterlacer.h"
#endif
#include "libretro.h"
#include <rthreads/rthreads.h>

#include "mednafen/pcfx/pcfx.h"
#include "mednafen/pcfx/soundbox.h"
#include "mednafen/pcfx/input.h"
#include "mednafen/pcfx/king.h"
#include "mednafen/pcfx/timer.h"
#include "mednafen/pcfx/interrupt.h"
#include "mednafen/pcfx/rainbow.h"
#ifdef HAVE_HUC6273
#include "mednafen/pcfx/huc6273.h"
#endif
#include "mednafen/cdrom/scsicd.h"
#include "mednafen/cdrom/cdromif.h"
#include "mednafen/md5.h"
#include "mednafen/clamp.h"
#include "mednafen/state_helpers.h"

#include "libretro_core_options.h"

#include "video_blit.h"
#include "sound_output.h"
#include "input_emu.h"
#include "main.h"
#include "shared.h"
#include "menu.h"
#include "config.h"

char GameName_emu[512];
uint8_t exit_vb = 0;
extern uint32_t emulator_state;

static MDFNGI *game;
std::string retro_base_directory;
std::string retro_save_directory;

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

static std::vector<CDIF*> *cdifs = NULL;
static bool CD_TrayOpen;
static int CD_SelectedDisc;	// -1 for no disc

V810 PCFX_V810;

static uint8 *BIOSROM = NULL; 	// 1MB
static uint8 *RAM = NULL; 	// 2MB

static uint32 RAM_LPA;		// Last page access

static const int RAM_PageSize = 2048;
static const int RAM_PageNOTMask = ~(RAM_PageSize - 1);

static uint16 Last_VDC_AR[2];

#ifdef HAVE_HUC6273
static bool WantHuC6273 = FALSE;
#endif

//static 
VDC *fx_vdc_chips[2];

static uint16 BackupControl;
static uint8 SaveRAM[2 * 0x8000]; // BackupRAM + ExBackupRAM
static uint8* BackupRAM = (uint8*)(SaveRAM + (0x8000 * 0));
static uint8* ExBackupRAM = (uint8*)(SaveRAM + (0x8000 * 1));
static uint8 ExBusReset; // I/O Register at 0x0700

static bool BRAMDisabled;	// Cached at game load, don't remove this caching behavior or save game loss may result(if we ever get a GUI).

// Checks to see if this main-RAM-area access
// is in the same DRAM page as the last access.
#define RAMLPCHECK	\
{					\
  if((A & RAM_PageNOTMask) != RAM_LPA)	\
  {					\
   timestamp += 3;			\
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

static INLINE uint32 CalcNextTS(void)
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
	//assert(next_timestamp > PCFX_V810.v810_timestamp);
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
	if(next_timestamp < PCFX_V810.GetEventNT())
		PCFX_V810.SetEventNT(next_timestamp);
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
	PCFX_V810.SetEventNT(CalcNextTS());
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
 //printf("%d\n", PCFX_V810.v810_timestamp);

 FXINPUT_Frame();

 KING_StartFrame(fx_vdc_chips, espec);	//espec->surface, &espec->DisplayRect, espec->LineWidths, espec->skip);

 v810_timestamp_t v810_timestamp;
 v810_timestamp = PCFX_V810.Run(pcfx_event_handler);


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
 v810_timestamp_t new_base_ts;
 espec->SoundBufSize = SoundBox_Flush(v810_timestamp, espec->SoundBuf, espec->SoundBufMaxSize);

 KING_ResetTS(new_base_ts);
 FXTIMER_ResetTS(new_base_ts);
 FXINPUT_ResetTS(new_base_ts);
 SoundBox_ResetTS();

 // Call this AFTER all the EndFrame/Flush/ResetTS stuff
 RebaseTS(v810_timestamp, new_base_ts);

 espec->MasterCycles = v810_timestamp - new_base_ts;

 PCFX_V810.ResetTS(new_base_ts);
}

static void PCFX_Reset(void)
{
 const uint32 timestamp = PCFX_V810.v810_timestamp;

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

 for(int i = 0; i < 2; i++)
 {
  int32 dummy_ne MDFN_NOWARN_UNUSED;

  dummy_ne = fx_vdc_chips[i]->Reset();
 }

 KING_Reset(timestamp);	// SCSICD_Power() is called from KING_Reset()
 SoundBox_Reset();
 RAINBOW_Reset();
#ifdef HAVE_HUC6273
 if(WantHuC6273)
  HuC6273_Reset();
#endif
 PCFXIRQ_Reset();
 FXTIMER_Reset();
 PCFX_V810.Reset();

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

static bool LoadCommon(std::vector<CDIF *> *CDInterfaces)
{
   std::string biospath    = MDFN_MakeFName(MDFNMKF_FIRMWARE, 0, MDFN_GetSettingS("pcfx.bios"));
   MDFNFILE *BIOSFile      = file_open(biospath.c_str());

	biospath = retro_base_directory + "/pcfx.rom";

   if(!BIOSFile)
   {
		printf("Can't load BIOS\n");
		return(0);
   }

   #ifdef HAVE_HUC6273
   if(EmuFlags & CDGE_FLAG_FXGA)
   {
      //WantHuC6273 = TRUE;
   }
   #endif

   PCFX_V810.Init();

   uint32 RAM_Map_Addresses[1] = { 0x00000000 };
   uint32 BIOSROM_Map_Addresses[1] = { 0xFFF00000 };

   RAM = PCFX_V810.SetFastMap(RAM_Map_Addresses, 0x00200000, 1, "RAM");

   // todo: cleanup on error
   if(!RAM)
      return(0);

   BIOSROM = PCFX_V810.SetFastMap(BIOSROM_Map_Addresses, 0x00100000, 1, "BIOS ROM");
   if(!BIOSROM)
      return(0);

   if(BIOSFile->size != 1024 * 1024)
   {
      MDFN_PrintError("BIOS ROM file is incorrect size.\n");
      return(0);
   }

   memcpy(BIOSROM, BIOSFile->data, 1024 * 1024);

   file_close(BIOSFile);
   BIOSFile = NULL;

   for(int i = 0; i < 2; i++)
   {
      fx_vdc_chips[i] = new VDC(MDFN_GetSettingB("pcfx.nospritelimit"), 65536);
      fx_vdc_chips[i]->SetWSHook(NULL);
      fx_vdc_chips[i]->SetIRQHook(i ? VDCB_IRQHook : VDCA_IRQHook);

      //fx_vdc_chips[0] = FXVDC_Init(PCFXIRQ_SOURCE_VDCA, MDFN_GetSettingB("pcfx.nospritelimit"));
      //fx_vdc_chips[1] = FXVDC_Init(PCFXIRQ_SOURCE_VDCB, MDFN_GetSettingB("pcfx.nospritelimit"));
   }

   SoundBox_Init(MDFN_GetSettingB("pcfx.adpcm.emulate_buggy_codec"), MDFN_GetSettingB("pcfx.adpcm.suppress_channel_reset_clicks"));
   RAINBOW_Init(MDFN_GetSettingB("pcfx.rainbow.chromaip"));
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
   CD_SelectedDisc = 0;

   SCSICD_SetDisc(true, NULL, true);
   SCSICD_SetDisc(false, (*CDInterfaces)[0], true);

   BRAMDisabled = MDFN_GetSettingB("pcfx.disable_bram");

   /*if(BRAMDisabled)
      MDFN_printf("Warning: BRAM is disabled per pcfx.disable_bram setting.  This is simulating a malfunction.\n");*/

   if(!BRAMDisabled)
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
   for(int i = 0; i < 256; i++)
   {
      PCFX_V810.SetMemReadBus32(i, FALSE);
      PCFX_V810.SetMemWriteBus32(i, FALSE);
   }

   // 16MiB RAM area.
   PCFX_V810.SetMemReadBus32(0, TRUE);
   PCFX_V810.SetMemWriteBus32(0, TRUE);

   // Bitstring read range
   for(int i = 0xA0; i <= 0xAF; i++)
   {
      PCFX_V810.SetMemReadBus32(i, FALSE);       // Reads to the read range are 16-bit, and
      PCFX_V810.SetMemWriteBus32(i, TRUE);       // writes are 32-bit.
   }

   // Bitstring write range
   for(int i = 0xB0; i <= 0xBF; i++)
   {
      PCFX_V810.SetMemReadBus32(i, TRUE);	// Reads to the write range are 32-bit,
      PCFX_V810.SetMemWriteBus32(i, FALSE);	// but writes are 16-bit!
   }

   // BIOS area
   for(int i = 0xF0; i <= 0xFF; i++)
   {
      PCFX_V810.SetMemReadBus32(i, FALSE);
      PCFX_V810.SetMemWriteBus32(i, FALSE);
   }

   PCFX_V810.SetMemReadHandlers(mem_rbyte, mem_rhword, mem_rword);
   PCFX_V810.SetMemWriteHandlers(mem_wbyte, mem_whword, mem_wword);

   PCFX_V810.SetIOReadHandlers(port_rbyte, port_rhword, NULL);
   PCFX_V810.SetIOWriteHandlers(port_wbyte, port_whword, NULL);



   return(1);
}

static void DoMD5CDVoodoo(std::vector<CDIF *> *CDInterfaces)
{
 const CDGameEntry *found_entry = NULL;
 TOC toc;

#if 0
 puts("{");
 puts(" ,");
 puts(" ,");
 puts(" 0,");
 puts(" 1,");
 puts(" {");
 puts("  {");

 for(int i = CDIF_GetFirstTrack(); i <= CDIF_GetLastTrack(); i++)
 {
    CDIF_Track_Format tf;

    CDIF_GetTrackFormat(i, tf);

    printf("   { %d, %s, %d },\n", i, (tf == CDIF_FORMAT_AUDIO) ? "CDIF_FORMAT_AUDIO" : "CDIF_FORMAT_MODE1", CDIF_GetTrackStartPositionLBA(i));
 }
 printf("   { -1, (CDIF_Track_Format)-1, %d },\n", CDIF_GetSectorCountLBA());
 puts("  }");
 puts(" }");
 puts("},");
 //exit(1);
#endif

 for(unsigned if_disc = 0; if_disc < CDInterfaces->size(); if_disc++)
 {
  (*CDInterfaces)[if_disc]->ReadTOC(&toc);

  if(toc.first_track == 1)
  {
   for(unsigned int g = 0; g < sizeof(GameList) / sizeof(CDGameEntry); g++)
   {
    const CDGameEntry *entry = &GameList[g];

    assert(entry->discs == 1 || entry->discs == 2);

    for(unsigned int disc = 0; disc < entry->discs; disc++)
    {
     const CDGameEntryTrack *et = entry->tracks[disc];
     bool GameFound = TRUE;

     while(et->tracknum != -1 && GameFound)
     {
      assert(et->tracknum > 0 && et->tracknum < 100);

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

   if(found_entry->discs > 1)
   {
    const char *hash_prefix = "Mednafen PC-FX Multi-Game Set";
    md5_context md5_gameset;

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
 } // end: for(unsigned if_disc = 0; if_disc < CDInterfaces->size(); if_disc++)
}

static int LoadCD(std::vector<CDIF *> *CDInterfaces)
{
 EmuFlags = 0;

 cdifs = CDInterfaces;

 DoMD5CDVoodoo(CDInterfaces);

 if(!LoadCommon(CDInterfaces))
  return(0);

 printf("Emulated CD-ROM drive speed: %ux\n", (unsigned int)MDFN_GetSettingUI("pcfx.cdspeed"));

 PCFX_Power();

 return(1);
}

static void PCFX_CDInsertEject(void)
{
 CD_TrayOpen = !CD_TrayOpen;

 for(unsigned disc = 0; disc < cdifs->size(); disc++)
 {
#if 0
  if(!(*cdifs)[disc]->Eject(CD_TrayOpen))
  {
   MDFN_DispMessage("Eject error.");
   CD_TrayOpen = !CD_TrayOpen;
  }
#endif
 }

 if(CD_TrayOpen)
  MDFN_DispMessage("Virtual CD Drive Tray Open");
 else
  MDFN_DispMessage("Virtual CD Drive Tray Closed");

 SCSICD_SetDisc(CD_TrayOpen, (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? (*cdifs)[CD_SelectedDisc] : NULL);
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

static void CloseGame(void)
{
   unsigned i;

   for(i = 0; i < 2; i++)
   {
      if(fx_vdc_chips[i])
      {
         delete fx_vdc_chips[i];
         fx_vdc_chips[i] = NULL;
      }
   }

   RAINBOW_Close();
   KING_Close();
   SoundBox_Kill();
   PCFX_V810.Kill();

   // The allocated memory RAM and BIOSROM is free'd in V810_Kill()
   RAM = NULL;
   BIOSROM = NULL;
}

static void DoSimpleCommand(int cmd)
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
}

extern "C" int StateAction(StateMem *sm, int load, int data_only)
{
   const v810_timestamp_t timestamp = PCFX_V810.v810_timestamp;

   SFORMAT StateRegs[] =
   {
      SFARRAY(RAM, 0x200000),
      SFARRAY16(Last_VDC_AR, 2),
      SFVAR(BackupControl),
      SFVAR(ExBusReset),
      SFARRAY(BackupRAM, BRAMDisabled ? 0 : 0x8000),
      SFARRAY(ExBackupRAM, BRAMDisabled ? 0 : 0x8000),

      SFVAR(CD_TrayOpen),
      SFVAR(CD_SelectedDisc),

      SFEND
   };

   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "MAIN", false);

   for(int i = 0; i < 2; i++)
      ret &= fx_vdc_chips[i]->StateAction(sm, load, data_only, i ? "VDC1" : "VDC0");

   ret &= FXINPUT_StateAction(sm, load, data_only);
   ret &= PCFXIRQ_StateAction(sm, load, data_only);
   ret &= KING_StateAction(sm, load, data_only);
   ret &= PCFX_V810.StateAction(sm, load, data_only);
   ret &= FXTIMER_StateAction(sm, load, data_only);
   ret &= SoundBox_StateAction(sm, load, data_only);
   ret &= SCSICD_StateAction(sm, load, data_only, "CDRM");
   ret &= RAINBOW_StateAction(sm, load, data_only);

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
         if(CD_SelectedDisc >= (int)cdifs->size())
            CD_SelectedDisc = (int)cdifs->size() - 1;

         SCSICD_SetDisc(CD_TrayOpen, (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? (*cdifs)[CD_SelectedDisc] : NULL, true);
      }
   }

   //printf("0x%08x, %d %d %d %d\n", load, next_pad_ts, next_timer_ts, next_adpcm_ts, next_king_ts);

   return(ret);
}

#ifdef NEED_DEINTERLACER
static bool PrevInterlaced;
static Deinterlacer deint;
#endif

#define MEDNAFEN_CORE_NAME_MODULE "pcfx"
#define MEDNAFEN_CORE_NAME "Beetle PC-FX"
#define MEDNAFEN_CORE_VERSION "v0.9.36.5"
#define MEDNAFEN_CORE_EXTENSIONS "cue|ccd|toc|chd"
#define MEDNAFEN_CORE_TIMING_FPS 59.94
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
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
   retro_base_directory = std::string(home_path);
   retro_save_directory = std::string(sram_path);
   setting_initial_scanline = 0;
   setting_last_scanline = 239;
}

void retro_reset(void)
{
   DoSimpleCommand(MDFN_MSC_RESET);
}


static float mouse_sensitivity = 1.25f;

static void check_variables(void)
{
	setting_high_dotclock_width = 256;
	setting_nospritelimit = 0;
	setting_resamp_quality = 0;
	setting_emulate_buggy_codec = 0;
	setting_suppress_channel_reset_clicks = 1;
	setting_rainbow_chromaip = 0;
}

#define MAX_PLAYERS 2
#define MAX_BUTTONS 15
static uint16_t input_buf[MAX_PLAYERS] = {0};
static int32_t  mousedata[MAX_PLAYERS][3] = {{0}, {0}};

static bool ReadM3U(std::vector<std::string> &file_list, std::string path, unsigned depth = 0)
{
   char linebuf[2048];
   std::string dir_path;
   std::vector<std::string> ret;
   FileWrapper m3u_file(path.c_str(), MODE_READ, "M3U CD Set");

   MDFN_GetFilePathComponents(path, &dir_path);

   while(m3u_file.get_line(linebuf, sizeof(linebuf)))
   {
      std::string efp;

      if(linebuf[0] == '#')
         continue;
      string_trim_whitespace_right(linebuf);
      if(linebuf[0] == 0)
         continue;

      efp = MDFN_EvalFIP(dir_path, std::string(linebuf));

      if(efp.size() >= 4 && efp.substr(efp.size() - 4) == ".m3u")
      {
         if(efp == path)
         {
            MDFN_Error(0, "M3U at \"%s\" references self.", efp.c_str());
            return false;
         }

         if(depth == 99)
         {
            MDFN_Error(0, "M3U load recursion too deep!");
            return false;
         }

         ReadM3U(file_list, efp, depth++);
      }
      else
         file_list.push_back(efp);
   }

   return true;
}

void MDFND_DispMessage(unsigned char *str)
{
   /*if (log_cb)
      log_cb(RETRO_LOG_INFO, "%s\n", str);*/
    //printf("%s\n", str);
}

void MDFN_ResetMessages(void)
{
}

 static std::vector<CDIF *> CDInterfaces;	// FIXME: Cleanup on error out.
// TODO: LoadCommon()

uint8_t MDFNI_LoadCD(const char *devicename)
{
 uint8 LayoutMD5[16];

 printf("Loading %s...\n", devicename);

  if(devicename && strlen(devicename) > 4 && !strcasecmp(devicename + strlen(devicename) - 4, ".m3u"))
  {
   std::vector<std::string> file_list;

   ReadM3U(file_list, devicename);

   for(unsigned i = 0; i < file_list.size(); i++)
   {
      CDIF *cdif   = CDIF_Open(file_list[i].c_str(), false /* cdimage_memcache */);
      CDInterfaces.push_back(cdif);
   }
  }
  else
  {
     CDIF *cdif   = CDIF_Open(devicename, false /* cdimage_memcache */);
   CDInterfaces.push_back(cdif);
  }

 //
 // Print out a track list for all discs.
 //
 for(unsigned i = 0; i < CDInterfaces.size(); i++)
 {
  TOC toc;

  CDInterfaces[i]->ReadTOC(&toc);

  printf("CD %d Layout:\n", i + 1);

  for(int32 track = toc.first_track; track <= toc.last_track; track++)
  {
   printf("Track %2d, LBA: %6d  %s\n", track, toc.tracks[track].lba, (toc.tracks[track].control & 0x4) ? "DATA" : "AUDIO");
  }

  printf("Leadout: %6d\n", toc.tracks[100].lba);
  printf("\n");
 }

 // Calculate layout MD5.  The system emulation LoadCD() code is free to ignore this value and calculate
 // its own, or to use it to look up a game in its database.
 {
  md5_context layout_md5;

  mednafen_md5_starts(&layout_md5);

  for(unsigned i = 0; i < CDInterfaces.size(); i++)
  {
   TOC toc;

   CDInterfaces[i]->ReadTOC(&toc);

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
 }

 printf("Using module: pcfx\n\n");

 if(!(LoadCD(&CDInterfaces)))
 {
  for(unsigned i = 0; i < CDInterfaces.size(); i++)
   delete CDInterfaces[i];
  CDInterfaces.clear();
  return(0);
 }

 //MDFNI_SetLayerEnableMask(~0ULL);

 MDFN_ResetMessages();   // Save state, status messages, etc.

 return 0;
}

static uint8_t MDFNI_LoadGame(const char *name)
{
	if(strlen(name) > 4 && (!strcasecmp(name + strlen(name) - 4, ".cue") || !strcasecmp(name + strlen(name) - 4, ".ccd") || !strcasecmp(name + strlen(name) - 4, ".chd") || !strcasecmp(name + strlen(name) - 4, ".toc") || !strcasecmp(name + strlen(name) - 4, ".m3u")))
	{
		return (MDFNI_LoadCD(name));
	}
	return 0;
}

bool Load_Game_Memory(char* path)
{
   check_variables();
	MDFNI_LoadGame(path);
#ifdef NEED_DEINTERLACER
	PrevInterlaced = false;
	deint.ClearState();
#endif

	switch(option.type_controller)
	{
		default:
			for (unsigned i = 0; i < MAX_PLAYERS; i++)
			{
				FXINPUT_SetInput(i, "gamepad", &input_buf[i]);
			}
		break;
		case 1:
			for (unsigned i = 0; i < MAX_PLAYERS; i++)
			{
				FXINPUT_SetInput(i, "mouse", &mousedata[i]);
			}
		break;
	}
	
	KING_SetPixelFormat();
	SoundBox_SetSoundRate(SOUND_OUTPUT_FREQUENCY);
	return 1;
}

static void MDFNI_CloseGame(void)
{
   CloseGame();

   for(unsigned i = 0; i < CDInterfaces.size(); i++)
      delete CDInterfaces[i];
   CDInterfaces.clear();
}

void Clean_Emu(void)
{
   if (!game)
      return;

   MDFNI_CloseGame();
}

static void update_input(void)
{
	input_buf[1] = 0;
	Read_General_Input();
	switch(option.type_controller)
	{
		default:
			input_buf[0] = Read_Pad_Input();
			mousedata[0][0] = 0;
			mousedata[0][1] = 0;
			mousedata[0][2] = 0;
		break;
		case 1:
			input_buf[0] = 0;
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


void Emulation_Run()
{
	EmulateSpecStruct spec = {0};
	static int16_t sound_buf[0x10000];
	static int32 rects[FB_MAX_HEIGHT];
	rects[0] = ~0;

	update_input();

	spec.surface = NULL;
	spec.SoundRate = SOUND_OUTPUT_FREQUENCY;
	spec.SoundBuf = sound_buf;
	spec.LineWidths = rects;
	spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
	spec.SoundVolume = 1.0;
	spec.soundmultiplier = 1.0;
	spec.SoundBufSize = 0;
	spec.VideoFormatChanged = false;
	spec.SoundFormatChanged = false;
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

#ifdef NEED_DEINTERLACER
   if (spec.InterlaceOn)
   {
      if (!PrevInterlaced)
         deint.ClearState();

      deint.Process(spec.surface, spec.DisplayRect, spec.LineWidths, spec.InterlaceField);

      PrevInterlaced = true;

      spec.InterlaceOn = false;
      spec.InterlaceField = 0;
   }
   else
      PrevInterlaced = false;
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

size_t retro_serialize_size(void)
{
   StateMem st;

   st.data           = NULL;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = 0;
   st.initial_malloc = 0;

   if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL))
      return 0;

   free(st.data);
   return serialize_size = st.len;
}

bool retro_serialize(void *data, size_t size)
{
   StateMem st;
   bool ret          = false;
   uint8_t *_dat     = (uint8_t*)malloc(size);

   if (!_dat)
      return false;

   st.data           = _dat;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = size;
   st.initial_malloc = 0;

   ret = MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);

   memcpy(data,st.data,size);
   free(st.data);
   return ret;
}

bool retro_unserialize(const void *data, size_t size)
{
   StateMem st;

   st.data           = (uint8_t*)data;
   st.loc            = 0;
   st.len            = size;
   st.malloced       = 0;
   st.initial_malloc = 0;

   return MDFNSS_LoadSM(&st, 0, 0);
}

#ifdef _WIN32
static void sanitize_path(std::string &path)
{
   size_t size = path.size();
   for (size_t i = 0; i < size; i++)
      if (path[i] == '/')
         path[i] = '\\';
}
#endif

// Use a simpler approach to make sure that things go right for libretro.
std::string MDFN_MakeFName(MakeFName_Type type, int id1, const char *cd1)
{
#ifdef _WIN32
   char slash = '\\';
#else
   char slash = '/';
#endif
   std::string ret;

   switch (type)
   {
      case MDFNMKF_FIRMWARE:
         ret = retro_base_directory + slash + std::string(cd1);
#ifdef _WIN32
         sanitize_path(ret); // Because Windows path handling is mongoloid.
#endif
         break;
      default:	  
         break;
   }

   /*if (log_cb)
      log_cb(RETRO_LOG_INFO, "MDFN_MakeFName: %s\n", ret.c_str());*/
   return ret;
}

void MDFND_MidSync(const EmulateSpecStruct *)
{}

void MDFN_MidLineUpdate(EmulateSpecStruct *espec, int y)
{
 //MDFND_MidLineUpdate(espec, y);
}

/* forward declarations */
extern void MDFND_DispMessage(unsigned char *str);

void MDFN_DispMessage(const char *format, ...)
{
}

static int curindent = 0;

void MDFN_indent(int indent)
{
	curindent += indent;
}

void MDFN_printf(const char *format, ...)
{
}

void MDFN_PrintError(const char *format, ...)
{
}


void SaveState(char* path, uint_fast8_t state)
{	
	FILE* savefp;
	size_t file_size;
	char* buffer = NULL;
	if (state == 1)
	{
		savefp = fopen(path, "rb");
		if (savefp)
		{
			fseek(savefp, 0, SEEK_END);
			file_size = ftell(savefp);
			fseek(savefp, 0, SEEK_SET);
			if (file_size > 0)
			{
				buffer = (char*)malloc(file_size);
				fread(buffer, sizeof(uint8_t), file_size, savefp);
				retro_unserialize(buffer, file_size);
			}
			fclose(savefp);
		}
	}
	else
	{
		savefp = fopen(path, "wb");
		if (savefp)
		{
			file_size = retro_serialize_size();
			buffer = (char*)malloc(file_size);
			retro_serialize(buffer, file_size);
			fwrite(buffer, sizeof(uint8_t), file_size, savefp);
			fclose(savefp);
		}
	}
	
	if (buffer)
	{
		free(buffer);
		buffer = NULL;
	}
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

/* Main entrypoint of the emulator */
int main(int argc, char* argv[])
{
	int isloaded;
	
	printf("Starting PCFXEmu\n");
    
	if (argc < 2)
	{
		printf("Specify a ROM to load in memory\n");
		return 0;
	}
	
	snprintf(GameName_emu, sizeof(GameName_emu), "%s", basename(argv[1]));

	Init_Configuration();
	
	Emu_Init();
	isloaded = Load_Game_Memory(argv[1]);
	if (!isloaded)
	{
		printf("Could not load ROM in memory\n");
		return 0;
	}
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
    
	Clean();
	Clean_Emu();
    Audio_Close();
    Video_Close();

    return 0;
}
