#include <stdlib.h>
#include <string.h>
#include "raw_audio.h"

static int raw_event_cmp(const void *ap, const void *bp)
{
    const PCFX_RawAudioEvent *a = (const PCFX_RawAudioEvent*)ap;
    const PCFX_RawAudioEvent *b = (const PCFX_RawAudioEvent*)bp;
    if(a->timestamp < b->timestamp) return -1;
    if(a->timestamp > b->timestamp) return 1;
    if(a->order < b->order) return -1;
    if(a->order > b->order) return 1;
    return 0;
}

static bool raw_reserve_events(PCFX_RawAudioMixer *mixer, int32 needed)
{
    if(needed <= mixer->event_capacity)
        return true;

    int32 newcap = mixer->event_capacity ? mixer->event_capacity : 4096;
    while(newcap < needed)
    {
        if(newcap > 1048576 / 2)
        {
            newcap = needed;
            break;
        }
        newcap *= 2;
    }

    PCFX_RawAudioEvent *ev = (PCFX_RawAudioEvent*)realloc(mixer->events, (size_t)newcap * sizeof(*ev));
    if(!ev)
        return false;
    mixer->events = ev;
    mixer->event_capacity = newcap;
    return true;
}

static inline int16 raw_filter_sample(PCFX_RawAudioMixer *mixer, int ch, int32 in_i)
{
    const double in = (double)in_i;
    double out = in - mixer->hp_prev_in[ch] + mixer->hp_coeff * mixer->hp_prev_out[ch];

    mixer->hp_prev_in[ch] = in;
    mixer->hp_prev_out[ch] = out;

    out *= mixer->output_gain;
    if(out < -32768.0) return -32768;
    if(out > 32767.0) return 32767;
    return (int16)((out >= 0.0) ? (out + 0.5) : (out - 0.5));
}

static void raw_emit_until(PCFX_RawAudioMixer *mixer, uint64 target_fp)
{
    while(mixer->next_sample_fp < target_fp)
    {
        if(mixer->frames < mixer->buffer_frames)
        {
            mixer->buffer[mixer->frames * 2 + 0] = raw_filter_sample(mixer, 0, mixer->current[0]);
            mixer->buffer[mixer->frames * 2 + 1] = raw_filter_sample(mixer, 1, mixer->current[1]);
            mixer->frames++;
        }
        else
            mixer->overflowed = true;

        mixer->next_sample_fp += mixer->step_fp;
    }
}

void PCFX_RawAudio_Init(PCFX_RawAudioMixer *mixer, uint32 clock_rate)
{
    if(!mixer) return;
    memset(mixer, 0, sizeof(*mixer));
    mixer->clock_rate = clock_rate ? clock_rate : 7159091;
    mixer->hp_coeff = 0.0;
    mixer->output_gain = 16.0;
}

void PCFX_RawAudio_Kill(PCFX_RawAudioMixer *mixer)
{
    if(!mixer) return;
    free(mixer->buffer);
    free(mixer->events);
    mixer->buffer = NULL;
    mixer->events = NULL;
    mixer->buffer_frames = 0;
    mixer->frames = 0;
    mixer->event_capacity = 0;
    mixer->event_count = 0;
    mixer->enabled = false;
}

bool PCFX_RawAudio_SetRate(PCFX_RawAudioMixer *mixer, uint32 sample_rate, int32 max_buffer_ms)
{
    if(!mixer) return false;
    if(!sample_rate)
    {
        mixer->enabled = false;
        mixer->sample_rate = 0;
        mixer->frames = 0;
        mixer->event_count = 0;
        return true;
    }

    if(max_buffer_ms <= 0) max_buffer_ms = 250;
    int64 frames64 = ((int64)sample_rate * max_buffer_ms + 999) / 1000;
    frames64 += 4096;
    if(frames64 < 4096) frames64 = 4096;
    if(frames64 > 262144) frames64 = 262144;

    int16 *newbuf = (int16*)realloc(mixer->buffer, (size_t)frames64 * 2 * sizeof(int16));
    if(!newbuf)
    {
        mixer->enabled = false;
        mixer->sample_rate = 0;
        mixer->buffer_frames = 0;
        mixer->frames = 0;
        return false;
    }

    mixer->buffer = newbuf;
    mixer->buffer_frames = (int32)frames64;
    mixer->sample_rate = sample_rate;
    mixer->step_fp = (((uint64)mixer->clock_rate) << 32) / sample_rate;
    if(!mixer->step_fp) mixer->step_fp = 1;

    /* Blip_Buffer_BassFreq(20) equivalent: remove long-lived DC while
       retaining the low-frequency content of CD-DA and ADPCM music.  Use
       a first-order approximation so the raw wasm build does not require
       libm. */
    mixer->hp_coeff = 1.0 - ((2.0 * 3.14159265358979323846 * 20.0) / (double)sample_rate);
    if(mixer->hp_coeff < 0.90 || mixer->hp_coeff > 0.9999)
        mixer->hp_coeff = 0.99715;
    mixer->enabled = true;
    mixer->frames = 0;
    mixer->event_count = 0;
    mixer->event_order = 0;
    mixer->dropped_events = 0;
    mixer->overflowed = false;
    mixer->next_sample_fp = 0;
    return true;
}

void PCFX_RawAudio_ResetTS(PCFX_RawAudioMixer *mixer, uint32 ts_base)
{
    if(!mixer) return;
    /* Timestamps are rebased by the core after each frame.  Preserve the
       fractional output scheduler across the boundary; clear only queued
       intra-frame deltas.  A non-zero base is still honored for state-load
       and reset paths. */
    if(ts_base)
        mixer->next_sample_fp = ((uint64)ts_base) << 32;
    mixer->event_count = 0;
    mixer->event_order = 0;
    mixer->overflowed = false;
}

void PCFX_RawAudio_ResetLevels(PCFX_RawAudioMixer *mixer)
{
    if(!mixer) return;
    mixer->current[0] = 0;
    mixer->current[1] = 0;
    mixer->hp_prev_in[0] = mixer->hp_prev_in[1] = 0.0;
    mixer->hp_prev_out[0] = mixer->hp_prev_out[1] = 0.0;
    mixer->event_count = 0;
    mixer->event_order = 0;
}

void PCFX_RawAudio_AddDelta(PCFX_RawAudioMixer *mixer, uint32 timestamp, int32 delta_l, int32 delta_r)
{
    if(!mixer || (!delta_l && !delta_r))
        return;

    if(!mixer->enabled || !mixer->buffer)
    {
        mixer->current[0] += delta_l;
        mixer->current[1] += delta_r;
        return;
    }

    if(!raw_reserve_events(mixer, mixer->event_count + 1))
    {
        mixer->dropped_events++;
        mixer->current[0] += delta_l;
        mixer->current[1] += delta_r;
        return;
    }

    PCFX_RawAudioEvent *ev = &mixer->events[mixer->event_count++];
    ev->timestamp = timestamp;
    ev->order = mixer->event_order++;
    ev->delta[0] = delta_l;
    ev->delta[1] = delta_r;
}

void PCFX_RawAudio_AdvanceTo(PCFX_RawAudioMixer *mixer, uint32 timestamp)
{
    if(!mixer || !mixer->enabled || !mixer->buffer)
        return;
    /* Kept for API compatibility.  Rendering is intentionally deferred to
       FlushFrame() so PSG, ADPCM, and CD-DA deltas can be time-sorted. */
    (void)timestamp;
}

int32 PCFX_RawAudio_FlushFrame(PCFX_RawAudioMixer *mixer, uint32 timestamp, int16 *out, int32 max_frames)
{
    if(!mixer || !out || max_frames <= 0)
        return 0;

    if(!mixer->enabled || !mixer->buffer)
    {
        mixer->event_count = 0;
        return 0;
    }

    if(mixer->event_count > 1)
        qsort(mixer->events, (size_t)mixer->event_count, sizeof(mixer->events[0]), raw_event_cmp);

    mixer->frames = 0;
    const uint64 frame_end_fp = ((uint64)timestamp) << 32;

    for(int32 i = 0; i < mixer->event_count; i++)
    {
        uint32 event_ts = mixer->events[i].timestamp;
        if(event_ts > timestamp)
            event_ts = timestamp;

        raw_emit_until(mixer, ((uint64)event_ts) << 32);
        mixer->current[0] += mixer->events[i].delta[0];
        mixer->current[1] += mixer->events[i].delta[1];
    }

    raw_emit_until(mixer, frame_end_fp);

    int32 count = mixer->frames;
    if(count > max_frames) count = max_frames;
    if(count > 0)
        memcpy(out, mixer->buffer, (size_t)count * 2 * sizeof(int16));

    if(mixer->frames > count)
    {
        memmove(mixer->buffer, mixer->buffer + count * 2, (size_t)(mixer->frames - count) * 2 * sizeof(int16));
        mixer->frames -= count;
    }
    else
        mixer->frames = 0;

    if(mixer->next_sample_fp >= frame_end_fp)
        mixer->next_sample_fp -= frame_end_fp;
    else
        mixer->next_sample_fp = 0;

    mixer->event_count = 0;
    mixer->event_order = 0;
    mixer->overflowed = false;
    return count;
}
