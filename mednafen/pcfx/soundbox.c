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

#include <math.h>
#include <string.h>

#include "shared.h"
#include "pcfx.h"
#include "soundbox.h"
#include "king.h"
#include "pce_psg/pce_psg.h"

#include "../cdrom/scsicd.h"
#include "mednafen/sound/raw_audio.h"
#include "../clamp.h"
#include "../state_helpers.h"


static void SoundBox_Clamp32(int32 *v, int32 lo, int32 hi)
{
 if(*v < lo) *v = lo;
 else if(*v > hi) *v = hi;
}

static void SoundBox_Clamp64(int64 *v, int64 lo, int64 hi)
{
 if(*v < lo) *v = lo;
 else if(*v > hi) *v = hi;
}

static const int StepSizes[49] =
{
	16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
	55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157,
	173, 190,  209, 230, 253, 279, 307, 337, 371, 408, 449,
	494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552
};

static const int StepIndexDeltas[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};

static PCFX_RawAudioMixer FXRawMixer;
PCFX_RawAudioMixer *SoundBox_GetRawMixer(void) { return &FXRawMixer; }

static PCE_PSG *pce_psg = NULL;
static bool SoundEnabled;
static uint32 adpcm_lastts;

static t_soundbox psg;
static float ADPCMVolTable[0x40];
static bool EmulateBuggyCodec = false;
static bool ResetAntiClickEnabled = false;

#ifdef PCFX_ADPCM_COMPAT_OPTIONS
void SoundBox_SetADPCMOptions(bool emulate_buggy_codec, bool suppress_channel_reset_clicks)
{
	EmulateBuggyCodec = emulate_buggy_codec;
	ResetAntiClickEnabled = suppress_channel_reset_clicks;
}

bool SoundBox_GetADPCMEmulateBuggyCodec(void)
{
	return EmulateBuggyCodec;
}

bool SoundBox_GetADPCMSuppressChannelResetClicks(void)
{
	return ResetAntiClickEnabled;
}
#endif

static void RedoVolume(void)
{
	PCE_PSG_SetVolume(pce_psg, 0.681f);	//0.227 * 0.50);
}

bool SoundBox_SetSoundRate(uint32 rate)
{
	SoundEnabled = (bool)rate;
#ifdef PCFX_HEADLESS
	if(!PCFX_RawAudio_SetRate(&FXRawMixer, rate ? rate : 44100, 250))
		return FALSE;
#else
	if(!PCFX_RawAudio_SetRate(&FXRawMixer, rate ? rate : 44100, 50))
		return FALSE;
#endif
	RedoVolume();
	return(TRUE);
}

int SoundBox_Init()
{
    uint_fast8_t x;
    SoundEnabled = false;
    /* Match the old PC-FX Blip buffer clock domain.  PSG, ADPCM, and CD-DA
       deltas are queued in the legacy v810/3 soundbox timestamp domain and
       sorted by the raw mixer before rendering each frame. */
    PCFX_RawAudio_Init(&FXRawMixer, 7159091);
    pce_psg = PCE_PSG_Create(&FXRawMixer, PCE_PSG_REVISION_HUC6280A);
    memset(&psg, 0, sizeof(psg));

    // Build ADPCM volume table, 1.5dB per step, ADPCM volume settings of 0x0 through 0x1B result in silence.
    for(x = 0; x < 0x40; x++)
	{
		float flub = 1;
		int vti = 0x3F - x;

		if(x) 
			flub /= pow(2, (float)1 / 4 * x);

		if(vti <= 0x1B)
			ADPCMVolTable[vti] = 0;
		else
			ADPCMVolTable[vti] = flub / 8;
	}
	return (1);
}

void SoundBox_Kill(void)
{
	if(pce_psg)
	{
		PCE_PSG_Destroy(pce_psg);
		pce_psg = NULL;
	}
	PCFX_RawAudio_Kill(&FXRawMixer);
}

/* VCA runs in the CPU timestamp domain (also advanced at -HSYNC). */
static v810_timestamp_t last_pcm_hsync_ts = 0;
static void SoundBox_PCMAdvance(v810_timestamp_t elapsed);

/* Macro to access currently selected PSG channel */
void SoundBox_Write(uint32 A, uint16 V, const v810_timestamp_t timestamp)
{
	A &= 0x3F;
    if(A < 0x20)
    {
		PCE_PSG_Write(pce_psg, timestamp / 3, A >> 1, V);
    }
	else
    {
         /* Keep CPU audio writes ordered against previously elapsed TETSU
            -HSYNC edges. Otherwise a deferred R10 write could incorrectly
            latch on an older edge when KING next catches up. */
         PCFX_SetEvent(PCFX_EVENT_KING, KING_Update(timestamp));
		 switch(A & 0x3F)
		 {
            case 0x20:
                /* HuC6230 §2.3: R10 is sampled on the falling edge of the
                   NEXT -HSYNC. Last write within a horizontal period wins. */
                psg.ADPCMPendingControl = V;
                psg.ADPCMPendingValid = true;
                break;

			case 0x22:
				SoundBox_ADPCMUpdate(timestamp);
				psg.ADPCMVolume[0][0] = V & 0x3F; 
			break;

			case 0x24:
				SoundBox_ADPCMUpdate(timestamp);
				psg.ADPCMVolume[0][1] = V & 0x3F;
			break;

			case 0x26:
				SoundBox_ADPCMUpdate(timestamp);
				psg.ADPCMVolume[1][0] = V & 0x3F;
			break;

			case 0x28:
				SoundBox_ADPCMUpdate(timestamp);
				psg.ADPCMVolume[1][1] = V & 0x3F; 
			break;

            case 0x2A:
                /* Advance the previous VCA target up to this exact CPU write. */
                SoundBox_PCMAdvance(timestamp - last_pcm_hsync_ts);
                last_pcm_hsync_ts = timestamp;
				psg.CDDAVolume[0] = V & 0x3F;
                /* Electronic VCA target only; actual level advances at HSYNC. */
			break;

            case 0x2C:
                SoundBox_PCMAdvance(timestamp - last_pcm_hsync_ts);
                last_pcm_hsync_ts = timestamp;
				psg.CDDAVolume[1] = V & 0x3F;
                /* Electronic VCA target only; actual level advances at HSYNC. */
			break;
		}
	}
}

static uint32 KINGADPCMControl;

/* The decoder's sample divider and the HuC6230's incoming 16-bit latch are
   different devices. A 16-bit transfer supplies four nibbles; the fastest
   31.47kHz nibble rate therefore needs a transfer every two 15.73kHz
   horizontal periods. Both channels share the same phase; R50 controls
   whether a channel supplies a word at that boundary. */
void SoundBox_ADPCMTransferHSync(void)
{
 const unsigned period = 2u << ((KINGADPCMControl >> 2) & 3u);
 /* R10 software reset holds the respective decoder and invalidates the
    captured transfer. A reset of both channels restarts the common phase. */
 unsigned eligible = KINGADPCMControl & 3u;
 for(unsigned ch = 0; ch < 2; ch++)
  if(psg.ADPCMControl & (0x10u << ch))
  {
   eligible &= ~(1u << ch);
   psg.ADPCMInputValid[ch] = false;
  }
 if(!psg.ADPCMTransferEstablished)
 {
  if(!eligible)
   return;
  psg.ADPCMTransferEstablished = true;
  psg.ADPCMTransferPhase = 0;
 }
 else
 {
  psg.ADPCMTransferPhase++;
  if(psg.ADPCMTransferPhase < period)
   return;
  psg.ADPCMTransferPhase = 0;
 }
 for(unsigned ch = 0; ch < 2; ch++)
 {
  if(eligible & (1u << ch))
  {
   /* Each channel has precisely one word latch. If the source supplies
      more than the decoder consumed, the latest captured word wins. */
   KING_RequestADPCMHalfWord(ch);
  }
 }
}

void SoundBox_ADPCMReceiveWord(int ch, uint16 value)
{
 if((unsigned)ch >= 2u) return;
 psg.ADPCMInputWord[ch]=value;
 psg.ADPCMInputValid[ch]=true;
}

void SoundBox_SetKINGADPCMControl(uint32 value)
{
	KINGADPCMControl = value;
}

/* HuC6230 §2.2.2: the PCM VCA advances in 1023 peak steps. Its
   step period doubles at each lower amplitude range. 32 V810 clocks are
   approximately 1.49us, matching the documented 3Dh->3Fh example.
   This is an HSYNC-quantized envelope, NOT an analog waveform simulation. */
static unsigned SoundBox_PCMTarget(uint8 reg)
{
 return reg == 0 ? 0u : (unsigned)reg * 16u + 15u;
}

static uint32 SoundBox_PCMPeriod(unsigned level)
{
 if(level >= 512u) return 32u;
 if(level >= 256u) return 64u;
 if(level >= 128u) return 128u;
 if(level >=  64u) return 256u;
 if(level >=  32u) return 512u;
 if(level >=  16u) return 1024u;
 return 4096u;  // special end-step region; approx. 7.5ms for full-scale fade
}

static void SoundBox_PCMAdvance(v810_timestamp_t elapsed)
{
 if(elapsed <= 0) return;
 bool changed = false;
 for(unsigned ch = 0; ch < 2; ch++)
 {
  const unsigned target = SoundBox_PCMTarget(psg.CDDAVolume[ch]);
  if(psg.CDDACurrentLevel[ch] == target)
  {
   psg.CDDAVolumeAccum[ch] = 0;
   continue;
  }
  uint32 accum = psg.CDDAVolumeAccum[ch] + (uint32)elapsed;
  unsigned current = psg.CDDACurrentLevel[ch];
  while(current != target)
  {
   const unsigned next = current < target ? current + 1u : current - 1u;
   const uint32 delay = SoundBox_PCMPeriod(current > next ? current : next);
   if(accum < delay) break;
   accum -= delay;
   current = next;
  }
  changed |= current != psg.CDDACurrentLevel[ch];
  psg.CDDACurrentLevel[ch] = (uint16)current;
  psg.CDDAVolumeAccum[ch] = current == target ? 0 : accum;
 }
 if(changed)
  SCSICD_SetCDDAVolume(0.50f * psg.CDDACurrentLevel[0] / 1023.0f,
                        0.50f * psg.CDDACurrentLevel[1] / 1023.0f);
}

/* Called at TETSU start-of-HBlank, the falling edge of active-low HSYNC.
   The V810 clock timestamp comes from the SAME timeline as register writes. */
void SoundBox_HSync(v810_timestamp_t timestamp)
{
 SoundBox_PCMAdvance(timestamp - last_pcm_hsync_ts);
 last_pcm_hsync_ts = timestamp;
 if(!psg.ADPCMPendingValid) return;

 SoundBox_ADPCMUpdate(timestamp);
 const uint16 V = psg.ADPCMPendingControl;
 psg.ADPCMPendingValid = false;
 for(unsigned ch = 0; ch < 2; ch++)
 {
  if(!(psg.ADPCMControl & (0x10u << ch)) && (V & (0x10u << ch)))
  {
   if(ResetAntiClickEnabled)
   {
    psg.ResetAntiClick[ch] += (int64)((uint64)psg.ADPCMPredictor[ch] << 32);
    SoundBox_Clamp64(&psg.ResetAntiClick[ch], -((int64)0x4000 << 32), ((int64)0x3FFF << 32));
   }
   psg.ADPCMPredictor[ch] = 0;
   psg.StepSizeIndex[ch] = 0;
   /* Reset the in-flight decompression/interpolation state as well as the
      scale and predictor. Otherwise a pre-reset delta can leak into the
      first held output or the next unrelated sound. */
   psg.ADPCMDelta[ch] = 0;
   psg.ADPCMHaveDelta[ch] = 0;
   psg.ADPCMWhichNibble[ch] = 0;
   psg.ADPCMHaveHalfWord[ch] = false;
   psg.ADPCMInputValid[ch] = false;
  }
 }
 /* HuC6230 §4.2: resetting both channels re-establishes the common
    input cycle. Single-channel reset must not rephase the other one. */
 if((V & 0x30u) == 0x30u && (psg.ADPCMControl & 0x30u) != 0x30u)
 {
  psg.ADPCMTransferEstablished = false;
  psg.ADPCMTransferPhase = 0;
  psg.ADPCMInputValid[0] = psg.ADPCMInputValid[1] = false;
 }
 psg.ADPCMControl = V;
}

/* Digital filter designed by mkfilter/mkshape/gencode   A.J. Fisher
   Command line: /www/usr/fisher/helpers/mkfilter -Bu -Lp -o 1 -a 1.5888889125e-04 0.0000000000e+00 -l */
static void DoVolumeFilter(int ch, int lr)
{
	psg.vf_xv[ch][lr][0] = psg.vf_xv[ch][lr][1]; 
	psg.vf_xv[ch][lr][1] = (float)ADPCMVolTable[psg.ADPCMVolume[ch][lr]] / 2.004348738e+03;

	psg.vf_yv[ch][lr][0] = psg.vf_yv[ch][lr][1]; 
	psg.vf_yv[ch][lr][1] = (psg.vf_xv[ch][lr][0] + psg.vf_xv[ch][lr][1]) + (  0.9990021696 * psg.vf_yv[ch][lr][0]);
	psg.VolumeFiltered[ch][lr] = psg.vf_yv[ch][lr][1];
}

v810_timestamp_t SoundBox_ADPCMUpdate(const v810_timestamp_t timestamp)
{
	int32 run_time = timestamp - adpcm_lastts;
	adpcm_lastts = timestamp;
	psg.bigdiv -= run_time * 2;

	while(psg.bigdiv <= 0)
	{
		psg.smalldiv--;
		while(psg.smalldiv <= 0)
		{
			psg.smalldiv += 1 << ((KINGADPCMControl >> 2) & 0x3);
			for(uint_fast8_t ch = 0; ch < 2; ch++)
			{
				// Keep playing our last halfword fetched even if KING ADPCM is disabled
				if(psg.ADPCMHaveHalfWord[ch] || psg.ADPCMInputValid[ch])
				{
						if(!psg.ADPCMWhichNibble[ch])
						{
						 if(!psg.ADPCMInputValid[ch])
						  continue; /* no valid two-byte capture: output-level hold */
						 psg.ADPCMHalfWord[ch] = psg.ADPCMInputWord[ch];
						 psg.ADPCMInputValid[ch] = false;
						 psg.ADPCMHaveHalfWord[ch] = TRUE;
						}

						// If the channel's reset bit is set, don't update its ADPCM state.
						if(psg.ADPCMControl & (0x10 << ch))
						{
							psg.ADPCMDelta[ch] = 0;
						}
						else
						{
							uint8 nibble = (psg.ADPCMHalfWord[ch] >> (psg.ADPCMWhichNibble[ch])) & 0xF;
							int32 BaseStepSize = StepSizes[psg.StepSizeIndex[ch]];
							
							if(EmulateBuggyCodec)
							{
								if(BaseStepSize == 1552)
									BaseStepSize = 1522;
								psg.ADPCMDelta[ch] = BaseStepSize * ((nibble & 0x7) + 1) * 2;
							}
							else
								psg.ADPCMDelta[ch] = BaseStepSize * ((nibble & 0x7) + 1);

							// Linear interpolation turned on?
							if(psg.ADPCMControl & (0x4 << ch))
								psg.ADPCMDelta[ch] >>= (KINGADPCMControl >> 2) & 0x3;

							if(nibble & 0x8)
								psg.ADPCMDelta[ch] = -psg.ADPCMDelta[ch];

							psg.StepSizeIndex[ch] += StepIndexDeltas[nibble];

							if(psg.StepSizeIndex[ch] < 0)
								psg.StepSizeIndex[ch] = 0;

							if(psg.StepSizeIndex[ch] > 48)
								psg.StepSizeIndex[ch] = 48;
						}
						psg.ADPCMHaveDelta[ch] = 1;

						// Linear interpolation turned on?
						if(psg.ADPCMControl & (0x4 << ch))
							psg.ADPCMHaveDelta[ch] = 1 << ((KINGADPCMControl >> 2) & 0x3);

						psg.ADPCMWhichNibble[ch] = (psg.ADPCMWhichNibble[ch] + 4) & 0xF;

						if(!psg.ADPCMWhichNibble[ch])
							psg.ADPCMHaveHalfWord[ch] = FALSE;
				}
			} // for(int ch...)
		} // while(psg.smalldiv <= 0)

		for(uint_fast8_t ch = 0; ch < 2; ch++)
		{
			//static int32 last_synthtime = 0;
			//uint32 synthtime = (timestamp + psg.bigdiv / 2) / 3;
			// This is as correct as the divide(I think), but may be faster(or maybe not?)
			uint32 synthtime = (timestamp - ((0 - psg.bigdiv) >> 1)) / 3;

			if(psg.ADPCMHaveDelta[ch]) 
			{
				psg.ADPCMPredictor[ch] += psg.ADPCMDelta[ch];
				psg.ADPCMHaveDelta[ch]--;

				if(psg.ADPCMPredictor[ch] > 0x3FFF) psg.ADPCMPredictor[ch] = 0x3FFF;
				if(psg.ADPCMPredictor[ch] < -0x4000) psg.ADPCMPredictor[ch] = -0x4000;
			}

			if(SoundEnabled)
			{
				int32 samp[2];
				if(EmulateBuggyCodec)
				{
					samp[0] = (int32)(((psg.ADPCMPredictor[ch] >> 1) + (psg.ResetAntiClick[ch] >> 33)) * psg.VolumeFiltered[ch][0]);
					samp[1] = (int32)(((psg.ADPCMPredictor[ch] >> 1) + (psg.ResetAntiClick[ch] >> 33)) * psg.VolumeFiltered[ch][1]);
				}
				else
				{
					samp[0] = (int32)((psg.ADPCMPredictor[ch] + (psg.ResetAntiClick[ch] >> 32)) * psg.VolumeFiltered[ch][0]);
					samp[1] = (int32)((psg.ADPCMPredictor[ch] + (psg.ResetAntiClick[ch] >> 32)) * psg.VolumeFiltered[ch][1]);
				}
				PCFX_RawAudio_AddDelta(&FXRawMixer, synthtime, (samp[0] - psg.ADPCM_last[ch][0]) / 2, (samp[1] - psg.ADPCM_last[ch][1]) / 2);
				psg.ADPCM_last[ch][0] = samp[0];
				psg.ADPCM_last[ch][1] = samp[1];
			}
		}

		for(uint_fast8_t ch = 0; ch < 2; ch++)
		{
			psg.ResetAntiClick[ch] -= psg.ResetAntiClick[ch] >> 8;
		}

		for(uint_fast8_t ch = 0; ch < 2; ch++)
		{
			for(uint_fast8_t lr = 0; lr < 2; lr++)
			{
				DoVolumeFilter(ch, lr);
			}
		}
		psg.bigdiv += 1365 * 2 / 2;
	}

	return(timestamp + (psg.bigdiv + 1) / 2);
}

int32 SoundBox_Flush(const uint32 v810_timestamp, int16 *SoundBuf, const int32 MaxSoundFrames)
{
	int32 timestamp;
	int32 FrameCount = 0;
	timestamp = v810_timestamp / 3;

	PCE_PSG_EndFrame(pce_psg, timestamp);

	if(SoundEnabled)
		FrameCount = PCFX_RawAudio_FlushFrame(&FXRawMixer, timestamp, SoundBuf, MaxSoundFrames);
	return(FrameCount);
}

void SoundBox_ResetTS(const v810_timestamp_t ts_base)
{
	//assert(adpcm_lastts == v810_timestamp);
	PCE_PSG_ResetTS(pce_psg, ts_base / 3);
	PCFX_RawAudio_ResetTS(&FXRawMixer, ts_base / 3);
	adpcm_lastts = 0;
 last_pcm_hsync_ts = 0;
}

void SoundBox_Reset(const v810_timestamp_t timestamp)
{
	SoundBox_ADPCMUpdate(timestamp);
	PCE_PSG_Power(pce_psg, timestamp / 3);
	PCFX_RawAudio_ResetLevels(&FXRawMixer);
	psg.ADPCMControl = 0;
 psg.ADPCMPendingControl = 0;
 psg.ADPCMPendingValid = false;
 memset(psg.CDDACurrentLevel, 0, sizeof(psg.CDDACurrentLevel));
 memset(psg.CDDAVolumeAccum, 0, sizeof(psg.CDDAVolumeAccum));
 psg.CDDAVolumeStateVersion = 1;
 last_pcm_hsync_ts = 0;
	memset(&psg.vf_xv, 0, sizeof(psg.vf_xv));
	memset(&psg.vf_yv, 0, sizeof(psg.vf_yv));

	for(uint_fast8_t ch = 0; ch < 2; ch++)
	{
		for(uint_fast8_t lr = 0; lr < 2; lr++)
		{
			psg.ADPCMVolume[ch][lr] = 0;
			psg.VolumeFiltered[ch][lr] = 0;
			psg.CDDAVolume[lr] = 0;
		}
		psg.ADPCMPredictor[ch] = 0;
		psg.StepSizeIndex[ch] = 0;
	}

	memset(psg.ADPCMWhichNibble, 0, sizeof(psg.ADPCMWhichNibble));
	memset(psg.ADPCMHalfWord, 0, sizeof(psg.ADPCMHalfWord));
	memset(psg.ADPCMHaveHalfWord, 0, sizeof(psg.ADPCMHaveHalfWord));
 memset(psg.ADPCMInputWord, 0, sizeof(psg.ADPCMInputWord));
 memset(psg.ADPCMInputValid, 0, sizeof(psg.ADPCMInputValid));
 psg.ADPCMTransferPhase = 0;
 psg.ADPCMTransferEstablished = false;
 psg.ADPCMInputStateVersion = 1;
	memset(psg.ADPCM_last, 0, sizeof(psg.ADPCM_last));

	SCSICD_SetCDDAVolume(0.50f * psg.CDDAVolume[0] / 63, 0.50f * psg.CDDAVolume[1] / 63);

	psg.bigdiv = 2;	// TODO: KING->SBOX ADPCM Synch //(1365 - 85 * 4) * 2; //1365 * 2 / 2;
	psg.smalldiv = 0;
	adpcm_lastts = 0;
}

int SoundBox_StateAction(StateMem *sm, int load, int data_only)
{
	int ret = 1;
 psg.CDDAVolumeStateVersion = load ? 0 : 1;
 psg.ADPCMInputStateVersion = load ? 0 : 1;

	ret &= PCE_PSG_StateAction(pce_psg, sm, load, data_only);

	SFORMAT SoundBox_StateRegs[] =
	{
		SFVARN(psg.ADPCMControl, "ADPCMControl"),
        SFVARN(psg.ADPCMPendingControl, "ADPCMPendingControl"),
        SFVARN(psg.ADPCMPendingValid, "ADPCMPendingValid"),
        SFARRAY16N(psg.CDDACurrentLevel, 2, "CDDACurrentLevel"),
        SFARRAY32N(psg.CDDAVolumeAccum, 2, "CDDAVolumeAccum"),
        SFVARN(psg.CDDAVolumeStateVersion, "CDDAVolumeStateVersion"),
        SFVARN(last_pcm_hsync_ts, "PCMHSYNCTimestamp"),
		SFARRAYN(&psg.ADPCMVolume[0][0], 2 * 2, "ADPCMVolume"),
		SFARRAYN(psg.CDDAVolume, 2, "CDDAVolume"),
		SFVARN(psg.bigdiv, "bigdiv"),
		SFVARN(psg.smalldiv, "smalldiv"),

		SFARRAY64N(&psg.ResetAntiClick[0], 2, "ResetAntiClick"),
		SFARRAYDN(&psg.VolumeFiltered[0][0], 2 * 2, "VolumeFiltered"),
		SFARRAYDN(&psg.vf_xv[0][0][0], 2 * 2 * (1 + 1), "vf_xv"),
		SFARRAYDN(&psg.vf_yv[0][0][0], 2 * 2 * (1 + 1), "vf_yv"),

		SFARRAY32N(psg.ADPCMDelta, 2, "ADPCMDelta"),
		SFARRAY32N(psg.ADPCMHaveDelta, 2, "ADPCMHaveDelta"),

		SFARRAY32N(&psg.ADPCMPredictor[0], 2, "ADPCMPredictor"),
		SFARRAY32N(&psg.StepSizeIndex[0], 2, "ADPCMStepSizeIndex"),

		SFARRAY32N(psg.ADPCMWhichNibble, 2, "ADPCMWNibble"),
		SFARRAY16N(psg.ADPCMHalfWord, 2, "ADPCMHalfWord"),
		SFARRAYBN(psg.ADPCMHaveHalfWord, 2, "ADPCMHHW"),
        SFARRAY16N(psg.ADPCMInputWord, 2, "ADPCMInputWord"),
        SFARRAYBN(psg.ADPCMInputValid, 2, "ADPCMInputValid"),
        SFVARN(psg.ADPCMTransferPhase, "ADPCMTransferPhase"),
        SFVARN(psg.ADPCMTransferEstablished, "ADPCMTransferEstablished"),
        SFVARN(psg.ADPCMInputStateVersion, "ADPCMInputStateVersion"),
		SFEND
	};
 
	ret &= MDFNSS_StateAction(sm, load, data_only, SoundBox_StateRegs, "SBOX", 0);
	if(load)
	{
		for(uint_fast8_t ch = 0; ch < 2; ch++)
		{
			SoundBox_Clamp32(&psg.ADPCMPredictor[ch], -0x4000, 0x3FFF);
			SoundBox_Clamp64(&psg.ResetAntiClick[ch], -((int64)0x4000 << 32), ((int64)0x3FFF << 32));

			if(!ResetAntiClickEnabled)
				psg.ResetAntiClick[ch] = 0;
			SoundBox_Clamp32(&psg.StepSizeIndex[ch], 0, 48);
			SoundBox_Clamp32(&psg.bigdiv, 1, 1365);
			SoundBox_Clamp32(&psg.smalldiv, 1, 8);

		}
        if(!psg.CDDAVolumeStateVersion)
        {
         /* Old states have only target registers and had immediate volume. */
         for(unsigned ch=0; ch<2; ch++)
          psg.CDDACurrentLevel[ch] = (uint16)SoundBox_PCMTarget(psg.CDDAVolume[ch]);
         memset(psg.CDDAVolumeAccum, 0, sizeof(psg.CDDAVolumeAccum));
         psg.ADPCMPendingValid = false;
        }
        psg.CDDAVolumeStateVersion = 1;
        if(!psg.ADPCMInputStateVersion)
        {
         /* Old states had no KRAM input latch. Preserve the in-progress
            decoder word and establish a fresh cycle at the next HSYNC. */
         memset(psg.ADPCMInputWord, 0, sizeof(psg.ADPCMInputWord));
         memset(psg.ADPCMInputValid, 0, sizeof(psg.ADPCMInputValid));
         psg.ADPCMTransferPhase = 0;
         psg.ADPCMTransferEstablished = false;
        }
        psg.ADPCMInputStateVersion = 1;
        psg.ADPCMTransferPhase &= 15u;
        last_pcm_hsync_ts = 0;
        SCSICD_SetCDDAVolume(0.50f * psg.CDDACurrentLevel[0] / 1023.0f,
                              0.50f * psg.CDDACurrentLevel[1] / 1023.0f);
	}
	return(ret); 
}
