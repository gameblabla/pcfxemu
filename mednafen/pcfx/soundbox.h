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

#include <Blip_Buffer.h>

extern Blip_Buffer FXsbuf[2];		// Used in the CDROM code

typedef struct {
    uint16 ADPCMControl;
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

    int32 ADPCM_last[2][2];
} t_soundbox;


bool SoundBox_SetSoundRate(uint32 rate);
int32 SoundBox_Flush(const uint32, int16 *SoundBuf, const int32 MaxSoundFrames);
void SoundBox_Write(uint32 A, uint16 V, const v810_timestamp_t timestamp);
int SoundBox_Init(void);

void SoundBox_Reset(const v810_timestamp_t timestamp);

int SoundBox_StateAction(StateMem *sm, int load, int data_only);

void SoundBox_SetKINGADPCMControl(uint32);

v810_timestamp_t SoundBox_ADPCMUpdate(const v810_timestamp_t timestamp);

void SoundBox_ResetTS(const v810_timestamp_t ts_base);

void SoundBox_Kill(void);


#include <mednafen/sound/Blip_Buffer.h>
#include <mednafen/sound/Stereo_Buffer.h>
#endif
