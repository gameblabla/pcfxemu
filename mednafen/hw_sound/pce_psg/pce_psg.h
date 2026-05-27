#ifndef _PCE_PSG_H
#define _PCE_PSG_H

#include "mednafen/mednafen.h"
#include "mednafen/sound/raw_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PCE_PSG PCE_PSG;
typedef struct psg_channel psg_channel;

typedef void (*PCE_PSG_UpdateOutputFunc)(PCE_PSG *pce, const int32 timestamp, psg_channel *ch);

struct psg_channel
{
 int32 counter;
 uint16 frequency;
 uint32 freq_cache;
 uint8 control;
 uint8 balance;
 uint8 waveform[32];
 uint8 waveform_index;
 uint8 dda;
 uint8 noisectrl;
 uint32 noise_freq_cache;
 int32 noisecount;
 uint32 lfsr;
 int32 vl[2];
 int samp_accum;
 int32 raw_prev_samp[2];
 int32 lastts;
 PCE_PSG_UpdateOutputFunc UpdateOutput;
};

enum
{
 PSG_GSREG_CH0_FREQ = 0x000,
 PSG_GSREG_CH0_CTRL,
 PSG_GSREG_CH0_BALANCE,
 PSG_GSREG_CH0_WINDEX,
 PSG_GSREG_CH0_SCACHE,
 PSG_GSREG_CH0_NCTRL,
 PSG_GSREG_CH0_LFSR,
 PSG_GSREG_CH1_FREQ = 0x100,
 PSG_GSREG_CH1_CTRL,
 PSG_GSREG_CH1_BALANCE,
 PSG_GSREG_CH1_WINDEX,
 PSG_GSREG_CH1_SCACHE,
 PSG_GSREG_CH1_NCTRL,
 PSG_GSREG_CH1_LFSR,
 PSG_GSREG_CH2_FREQ = 0x200,
 PSG_GSREG_CH2_CTRL,
 PSG_GSREG_CH2_BALANCE,
 PSG_GSREG_CH2_WINDEX,
 PSG_GSREG_CH2_SCACHE,
 PSG_GSREG_CH2_NCTRL,
 PSG_GSREG_CH2_LFSR,
 PSG_GSREG_CH3_FREQ = 0x300,
 PSG_GSREG_CH3_CTRL,
 PSG_GSREG_CH3_BALANCE,
 PSG_GSREG_CH3_WINDEX,
 PSG_GSREG_CH3_SCACHE,
 PSG_GSREG_CH3_NCTRL,
 PSG_GSREG_CH3_LFSR,
 PSG_GSREG_CH4_FREQ = 0x400,
 PSG_GSREG_CH4_CTRL,
 PSG_GSREG_CH4_BALANCE,
 PSG_GSREG_CH4_WINDEX,
 PSG_GSREG_CH4_SCACHE,
 PSG_GSREG_CH4_NCTRL,
 PSG_GSREG_CH4_LFSR,
 PSG_GSREG_CH5_FREQ = 0x500,
 PSG_GSREG_CH5_CTRL,
 PSG_GSREG_CH5_BALANCE,
 PSG_GSREG_CH5_WINDEX,
 PSG_GSREG_CH5_SCACHE,
 PSG_GSREG_CH5_NCTRL,
 PSG_GSREG_CH5_LFSR,
 PSG_GSREG_SELECT = 0x1000,
 PSG_GSREG_GBALANCE,
 PSG_GSREG_LFOFREQ,
 PSG_GSREG_LFOCTRL,
 _PSG_GSREG_COUNT
};

enum
{
 PCE_PSG_REVISION_HUC6280 = 0,
 PCE_PSG_REVISION_HUC6280A,
 PCE_PSG_REVISION_ENHANCED,
 PCE_PSG_REVISION_COUNT
};

struct PCE_PSG
{
 uint8 select;
 uint8 globalbalance;
 uint8 lfofreq;
 uint8 lfoctrl;
 int32 vol_update_counter;
 int32 vol_update_which;
 int32 vol_update_vllatch;
 bool vol_pending;
 psg_channel channel[6];
 int32 lastts;
 int revision;
 bool SoundEnabled;
 PCFX_RawAudioMixer *mixer;
 float volume;
 int32 dbtable_volonly[32];
 int32 dbtable[32][32];
};

PCE_PSG *PCE_PSG_Create(PCFX_RawAudioMixer *mixer, int want_revision);
void PCE_PSG_Destroy(PCE_PSG *pce);
int PCE_PSG_StateAction(PCE_PSG *pce, StateMem *sm, int load, int data_only);
void PCE_PSG_Power(PCE_PSG *pce, const int32 timestamp);
void PCE_PSG_Write(PCE_PSG *pce, int32 timestamp, uint8 A, uint8 V);
void PCE_PSG_SetVolume(PCE_PSG *pce, float new_volume);
void PCE_PSG_EndFrame(PCE_PSG *pce, int32 timestamp);
void PCE_PSG_SetRegister(PCE_PSG *pce, const unsigned int id, const uint32 value);
void PCE_PSG_PeekWave(PCE_PSG *pce, const unsigned int ch, uint32 Address, uint32 Length, uint8 *Buffer);
void PCE_PSG_PokeWave(PCE_PSG *pce, const unsigned int ch, uint32 Address, uint32 Length, const uint8 *Buffer);
void PCE_PSG_ResetTS(PCE_PSG *pce, int32 ts_base);

#ifdef __cplusplus
}
#endif

#endif
