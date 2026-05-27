#ifndef PCFX_RAW_AUDIO_H
#define PCFX_RAW_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "mednafen/mednafen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PCFX_RawAudioEvent
{
    uint32 timestamp;
    uint32 order;
    int32 delta[2];
} PCFX_RawAudioEvent;

typedef struct PCFX_RawAudioMixer
{
    uint32 sample_rate;
    uint32 clock_rate;
    bool enabled;

    /* Current DC level carried across frame boundaries. */
    int32 current[2];

    /* Fractional sample scheduler, relative to the current emulation frame. */
    uint64 next_sample_fp;
    uint64 step_fp;

    /* Output DC blocker.  The old Blip buffer path used BassFreq(20),
       which prevented stopped CD-DA/PSG/ADPCM levels from becoming a
       permanent DC offset. */
    double hp_prev_in[2];
    double hp_prev_out[2];
    double hp_coeff;
    double output_gain;

    int16 *buffer;
    int32 buffer_frames;
    int32 frames;
    bool overflowed;

    PCFX_RawAudioEvent *events;
    int32 event_count;
    int32 event_capacity;
    uint32 event_order;
    uint32 dropped_events;
} PCFX_RawAudioMixer;

void PCFX_RawAudio_Init(PCFX_RawAudioMixer *mixer, uint32 clock_rate);
void PCFX_RawAudio_Kill(PCFX_RawAudioMixer *mixer);
bool PCFX_RawAudio_SetRate(PCFX_RawAudioMixer *mixer, uint32 sample_rate, int32 max_buffer_ms);
void PCFX_RawAudio_ResetTS(PCFX_RawAudioMixer *mixer, uint32 ts_base);
void PCFX_RawAudio_ResetLevels(PCFX_RawAudioMixer *mixer);
void PCFX_RawAudio_AddDelta(PCFX_RawAudioMixer *mixer, uint32 timestamp, int32 delta_l, int32 delta_r);
void PCFX_RawAudio_AdvanceTo(PCFX_RawAudioMixer *mixer, uint32 timestamp);
int32 PCFX_RawAudio_FlushFrame(PCFX_RawAudioMixer *mixer, uint32 timestamp, int16 *out, int32 max_frames);

#ifdef __cplusplus
}
#endif

#endif
