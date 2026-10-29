/******************************************************************************/
/* Mednafen NEC PC-FX Emulation Module                                        */
/******************************************************************************/
/* soundbox.h:
**  Copyright (C) 2006-2016 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef _PCFX_SOUNDBOX_H
#define _PCFX_SOUNDBOX_H

#include "mednafen/sound/raw_audio.h"

#ifdef __cplusplus
extern "C" {
#endif
PCFX_RawAudioMixer *SoundBox_GetRawMixer(void);

typedef struct {
    uint16 ADPCMControl;
    uint16 ADPCMPendingControl; // R10 is sampled at the next falling -HSYNC
    bool ADPCMPendingValid;
    uint16 CDDACurrentLevel[2]; // electronic VCA, 0..1023 peak steps
    uint32 CDDAVolumeAccum[2];  // partial V810 cycles toward the next step
    uint8 CDDAVolumeStateVersion;
    uint8 ADPCMVolume[2][2]; // ADPCMVolume[channel(0 or 1)][left(0) or right(1)]
    uint8 CDDAVolume[2];
    int32 bigdiv;
    int32 smalldiv;

    int64 ResetAntiClick[2];
    float VolumeFiltered[2][2];
    float vf_xv[2][2][1+1], vf_yv[2][2][1+1];

    int32 ADPCMDelta[2];
    int32 ADPCMHaveDelta[2];

    int32 ADPCMPredictor[2];
    int32 StepSizeIndex[2];

    uint32 ADPCMWhichNibble[2];
    uint16 ADPCMHalfWord[2];
    bool ADPCMHaveHalfWord[2];
    /* HuC6230 §4.2: each channel receives a two-byte capture at a shared
       -HSYNC-synchronized transfer-cycle boundary. The nibble decoder does
       not directly access KING/KRAM. */
    uint16 ADPCMInputWord[2];
    bool ADPCMInputValid[2];
    uint8 ADPCMTransferPhase;          /* elapsed HSYNCs since the last capture */
    bool ADPCMTransferEstablished;
    uint8 ADPCMInputStateVersion;

    int32 ADPCM_last[2][2];
} t_soundbox;


bool SoundBox_SetSoundRate(uint32 rate);
int32 SoundBox_Flush(const uint32, int16 *SoundBuf, const int32 MaxSoundFrames);
void SoundBox_Write(uint32 A, uint16 V, const v810_timestamp_t timestamp);
int SoundBox_Init(void);

#ifdef PCFX_ADPCM_COMPAT_OPTIONS
void SoundBox_SetADPCMOptions(bool emulate_buggy_codec, bool suppress_channel_reset_clicks);
bool SoundBox_GetADPCMEmulateBuggyCodec(void);
bool SoundBox_GetADPCMSuppressChannelResetClicks(void);
#endif

void SoundBox_Reset(const v810_timestamp_t timestamp);

int SoundBox_StateAction(StateMem *sm, int load, int data_only);

void SoundBox_SetKINGADPCMControl(uint32);
void SoundBox_HSync(v810_timestamp_t timestamp);
/* Invoke after KING has latched new R50 enables on the same -HSYNC edge. */
void SoundBox_ADPCMTransferHSync(void);
void SoundBox_ADPCMReceiveWord(int ch, uint16 value);

v810_timestamp_t SoundBox_ADPCMUpdate(const v810_timestamp_t timestamp);

void SoundBox_ResetTS(const v810_timestamp_t ts_base);

void SoundBox_Kill(void);


#ifdef __cplusplus
}
#endif
#endif
