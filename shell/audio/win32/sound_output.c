#define COBJMACROS
#define INITGUID
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(PCFX_WIN32_HAVE_WASAPI)
#include <initguid.h>
#endif
#include <mmsystem.h>
#include <mmreg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#if defined(PCFX_WIN32_HAVE_WASAPI)
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif

#include "sound_output.h"
#include "sound_output_win32.h"

#define PCFX_WAVE_BLOCKS 4
#define PCFX_WAVE_SAMPLES SOUND_SAMPLES_SIZE
#define PCFX_WASAPI_BUFFER_MS 120

#ifndef AUDCLNT_BUFFERFLAGS_SILENT
#define AUDCLNT_BUFFERFLAGS_SILENT 0x2
#endif

#ifndef AUDCLNT_SHAREMODE_SHARED
#define AUDCLNT_SHAREMODE_SHARED 0
#endif
#ifndef AUDCLNT_SHAREMODE_EXCLUSIVE
#define AUDCLNT_SHAREMODE_EXCLUSIVE 1
#endif

/* WAVEOUT backend -------------------------------------------------------- */

typedef struct Win32AudioBlock
{
    WAVEHDR hdr;
    int16_t samples[PCFX_WAVE_SAMPLES * 2];
} Win32AudioBlock;

static HWAVEOUT g_waveout;
static Win32AudioBlock g_blocks[PCFX_WAVE_BLOCKS];
static unsigned g_block_index;
static int g_wave_ready;

/* WASAPI backend --------------------------------------------------------- */

#if defined(PCFX_WIN32_HAVE_WASAPI)
typedef struct WasapiState
{
    IMMDeviceEnumerator* enumerator;
    IMMDevice* device;
    IAudioClient* client;
    IAudioRenderClient* render;
    UINT32 buffer_frames;
    int com_initialized;
    int running;
    int exclusive;
} WasapiState;

static WasapiState g_wasapi;
#endif

static void waveout_close(void);
static void audio_apply_mute_state(void);

static int g_selected_backend = PCFX_WIN32_AUDIO_WAVEOUT;
static int g_active_backend = -1;
static char g_audio_error[384];
static int g_audio_muted;

static void audio_set_error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_audio_error, sizeof(g_audio_error), fmt, ap);
    va_end(ap);
}

static void audio_clear_error(void)
{
    g_audio_error[0] = 0;
}

static WAVEFORMATEX pcfx_audio_format(void)
{
    WAVEFORMATEX wf;
    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = SOUND_OUTPUT_FREQUENCY;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(wf.nChannels * (wf.wBitsPerSample / 8));
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    return wf;
}

static int waveout_init(void)
{
    WAVEFORMATEX wf = pcfx_audio_format();
    MMRESULT mmr = waveOutOpen(&g_waveout, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
    if(mmr != MMSYSERR_NOERROR)
    {
        g_waveout = NULL;
        audio_set_error("waveOutOpen failed with MMRESULT %u.", (unsigned)mmr);
        return 1;
    }

    memset(g_blocks, 0, sizeof(g_blocks));
    for(unsigned i = 0; i < PCFX_WAVE_BLOCKS; i++)
    {
        g_blocks[i].hdr.lpData = (LPSTR)g_blocks[i].samples;
        g_blocks[i].hdr.dwBufferLength = sizeof(g_blocks[i].samples);
        g_blocks[i].hdr.dwFlags = 0;
        mmr = waveOutPrepareHeader(g_waveout, &g_blocks[i].hdr, sizeof(WAVEHDR));
        if(mmr != MMSYSERR_NOERROR)
        {
            audio_set_error("waveOutPrepareHeader failed with MMRESULT %u.", (unsigned)mmr);
            waveout_close();
            return 1;
        }
    }
    g_block_index = 0;
    g_wave_ready = 1;
    return 0;
}

static void waveout_write(int16_t* buffer, uint32_t buffer_size)
{
    if(g_audio_muted)
        return;
    if(!g_wave_ready || !g_waveout || !buffer || !buffer_size)
        return;

    uint32_t frames_left = buffer_size;
    const int16_t* src = buffer;
    unsigned wait_ms = 0;

    while(frames_left)
    {
        Win32AudioBlock* block = &g_blocks[g_block_index];

        /* A freshly prepared WAVEHDR is free but is not required to carry
         * WHDR_DONE.  WHDR_INQUEUE is the reliable busy indication.  The old
         * code tested WHDR_DONE and could therefore drop every buffer on Wine
         * and some WinMM implementations. */
        if(block->hdr.dwFlags & WHDR_INQUEUE)
        {
            if(wait_ms++ > 100)
                return;
            Sleep(1);
            continue;
        }

        uint32_t frames = frames_left;
        if(frames > PCFX_WAVE_SAMPLES)
            frames = PCFX_WAVE_SAMPLES;

        memcpy(block->samples, src, frames * 2u * sizeof(int16_t));
        block->hdr.dwBufferLength = frames * 2u * sizeof(int16_t);

        MMRESULT mmr = waveOutWrite(g_waveout, &block->hdr, sizeof(WAVEHDR));
        if(mmr != MMSYSERR_NOERROR)
        {
            audio_set_error("waveOutWrite failed with MMRESULT %u.", (unsigned)mmr);
            return;
        }

        src += frames * 2u;
        frames_left -= frames;
        g_block_index = (g_block_index + 1) % PCFX_WAVE_BLOCKS;
        wait_ms = 0;
    }
}

static void waveout_close(void)
{
    if(!g_wave_ready && !g_waveout)
        return;
    g_wave_ready = 0;
    if(g_waveout)
    {
        waveOutReset(g_waveout);
        for(unsigned i = 0; i < PCFX_WAVE_BLOCKS; i++)
        {
            if(g_blocks[i].hdr.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(g_waveout, &g_blocks[i].hdr, sizeof(WAVEHDR));
        }
        waveOutClose(g_waveout);
        g_waveout = NULL;
    }
}

#if defined(PCFX_WIN32_HAVE_WASAPI)
/* WASAPI implementation.  It intentionally feeds the same stereo signed
 * 16-bit stream that the core already produces.  The mode selection is runtime
 * UI state, but the video bpp remains compile-time-only elsewhere. */

static void wasapi_release(void)
{
    if(g_wasapi.client && g_wasapi.running)
        IAudioClient_Stop(g_wasapi.client);
    if(g_wasapi.render)
    {
        IAudioRenderClient_Release(g_wasapi.render);
        g_wasapi.render = NULL;
    }
    if(g_wasapi.client)
    {
        IAudioClient_Release(g_wasapi.client);
        g_wasapi.client = NULL;
    }
    if(g_wasapi.device)
    {
        IMMDevice_Release(g_wasapi.device);
        g_wasapi.device = NULL;
    }
    if(g_wasapi.enumerator)
    {
        IMMDeviceEnumerator_Release(g_wasapi.enumerator);
        g_wasapi.enumerator = NULL;
    }
    if(g_wasapi.com_initialized)
        CoUninitialize();
    memset(&g_wasapi, 0, sizeof(g_wasapi));
}

static int wasapi_check_format(IAudioClient* client, int exclusive, WAVEFORMATEX* wf)
{
    WAVEFORMATEX* closest = NULL;
    HRESULT hr;
    if(exclusive)
        hr = IAudioClient_IsFormatSupported(client, AUDCLNT_SHAREMODE_EXCLUSIVE, wf, NULL);
    else
        hr = IAudioClient_IsFormatSupported(client, AUDCLNT_SHAREMODE_SHARED, wf, &closest);
    if(closest)
        CoTaskMemFree(closest);
    if(hr == S_OK)
        return 1;
    audio_set_error("WASAPI %s does not accept %u Hz stereo signed 16-bit PCM on the default render device (HRESULT 0x%08lX).",
                    exclusive ? "exclusive" : "shared",
                    (unsigned)wf->nSamplesPerSec,
                    (unsigned long)hr);
    return 0;
}

static int wasapi_init(int exclusive)
{
    HRESULT hr;
    WAVEFORMATEX wf = pcfx_audio_format();
    REFERENCE_TIME hns_buffer = (REFERENCE_TIME)((10000LL * PCFX_WASAPI_BUFFER_MS));

    memset(&g_wasapi, 0, sizeof(g_wasapi));
    g_wasapi.exclusive = exclusive ? 1 : 0;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if(SUCCEEDED(hr))
        g_wasapi.com_initialized = 1;
    else if(hr != RPC_E_CHANGED_MODE)
    {
        audio_set_error("CoInitializeEx failed for WASAPI (HRESULT 0x%08lX).", (unsigned long)hr);
        wasapi_release();
        return 1;
    }

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void**)&g_wasapi.enumerator);
    if(FAILED(hr))
    {
        audio_set_error("Could not create the WASAPI device enumerator (HRESULT 0x%08lX).", (unsigned long)hr);
        wasapi_release();
        return 1;
    }

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(g_wasapi.enumerator, eRender, eConsole, &g_wasapi.device);
    if(FAILED(hr))
    {
        audio_set_error("Could not get the default WASAPI render endpoint (HRESULT 0x%08lX).", (unsigned long)hr);
        wasapi_release();
        return 1;
    }

    hr = IMMDevice_Activate(g_wasapi.device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&g_wasapi.client);
    if(FAILED(hr))
    {
        audio_set_error("Could not activate IAudioClient (HRESULT 0x%08lX).", (unsigned long)hr);
        wasapi_release();
        return 1;
    }

    if(!wasapi_check_format(g_wasapi.client, exclusive, &wf))
    {
        wasapi_release();
        return 1;
    }

    hr = IAudioClient_Initialize(g_wasapi.client,
                                 exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED,
                                 0,
                                 hns_buffer,
                                 0,
                                 &wf,
                                 NULL);
    if(FAILED(hr))
    {
        audio_set_error("IAudioClient::Initialize failed for WASAPI %s mode (HRESULT 0x%08lX).",
                        exclusive ? "exclusive" : "shared",
                        (unsigned long)hr);
        wasapi_release();
        return 1;
    }

    hr = IAudioClient_GetBufferSize(g_wasapi.client, &g_wasapi.buffer_frames);
    if(FAILED(hr) || !g_wasapi.buffer_frames)
    {
        audio_set_error("IAudioClient::GetBufferSize failed (HRESULT 0x%08lX).", (unsigned long)hr);
        wasapi_release();
        return 1;
    }

    hr = IAudioClient_GetService(g_wasapi.client, &IID_IAudioRenderClient, (void**)&g_wasapi.render);
    if(FAILED(hr))
    {
        audio_set_error("IAudioClient::GetService(IAudioRenderClient) failed (HRESULT 0x%08lX).", (unsigned long)hr);
        wasapi_release();
        return 1;
    }

    BYTE* silence = NULL;
    hr = IAudioRenderClient_GetBuffer(g_wasapi.render, g_wasapi.buffer_frames, &silence);
    if(SUCCEEDED(hr))
        IAudioRenderClient_ReleaseBuffer(g_wasapi.render, g_wasapi.buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);

    hr = IAudioClient_Start(g_wasapi.client);
    if(FAILED(hr))
    {
        audio_set_error("IAudioClient::Start failed (HRESULT 0x%08lX).", (unsigned long)hr);
        wasapi_release();
        return 1;
    }

    g_wasapi.running = 1;
    return 0;
}

static void wasapi_write(int16_t* buffer, uint32_t buffer_size)
{
    if(g_audio_muted)
        return;
    if(!g_wasapi.render || !g_wasapi.client || !buffer || !buffer_size)
        return;

    const int16_t* src = buffer;
    uint32_t frames_left = buffer_size;
    const UINT32 frame_bytes = 2u * sizeof(int16_t);

    while(frames_left)
    {
        UINT32 padding = 0;
        HRESULT hr = IAudioClient_GetCurrentPadding(g_wasapi.client, &padding);
        if(FAILED(hr) || padding >= g_wasapi.buffer_frames)
            return;

        UINT32 available = g_wasapi.buffer_frames - padding;
        if(!available)
            return;

        UINT32 frames = frames_left;
        if(frames > available)
            frames = available;

        BYTE* dst = NULL;
        hr = IAudioRenderClient_GetBuffer(g_wasapi.render, frames, &dst);
        if(FAILED(hr) || !dst)
            return;

        memcpy(dst, src, frames * frame_bytes);
        IAudioRenderClient_ReleaseBuffer(g_wasapi.render, frames, 0);

        src += frames * 2u;
        frames_left -= frames;
    }
}

#else
static int wasapi_init(int exclusive)
{
    (void)exclusive;
    audio_set_error("WASAPI is not compiled into this build; use WaveOut.");
    return 1;
}
static void wasapi_write(int16_t* buffer, uint32_t buffer_size)
{
    (void)buffer;
    (void)buffer_size;
}
static void wasapi_release(void) { }
#endif /* PCFX_WIN32_HAVE_WASAPI */


static void audio_apply_mute_state(void)
{
    if(g_active_backend == PCFX_WIN32_AUDIO_WAVEOUT)
    {
        if(g_waveout)
            waveOutReset(g_waveout);
        g_block_index = 0;
        return;
    }

#if defined(PCFX_WIN32_HAVE_WASAPI)
    if(g_active_backend == PCFX_WIN32_AUDIO_WASAPI_SHARED ||
       g_active_backend == PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE)
    {
        if(!g_wasapi.client)
            return;
        if(g_audio_muted)
        {
            if(g_wasapi.running)
            {
                IAudioClient_Stop(g_wasapi.client);
                g_wasapi.running = 0;
            }
            IAudioClient_Reset(g_wasapi.client);
        }
        else
        {
            if(!g_wasapi.running)
            {
                HRESULT hr = IAudioClient_Start(g_wasapi.client);
                if(SUCCEEDED(hr))
                    g_wasapi.running = 1;
                else
                    audio_set_error("IAudioClient::Start failed while unmuting (HRESULT 0x%08lX).", (unsigned long)hr);
            }
        }
    }
#endif
}

/* Public frontend API ---------------------------------------------------- */

void PCFX_Win32_AudioSetBackend(int backend)
{
#if defined(PCFX_WIN32_HAVE_WASAPI)
    if(backend < PCFX_WIN32_AUDIO_WAVEOUT || backend > PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE)
        backend = PCFX_WIN32_AUDIO_WAVEOUT;
#else
    backend = PCFX_WIN32_AUDIO_WAVEOUT;
#endif
    g_selected_backend = backend;
}

int PCFX_Win32_AudioGetBackend(void)
{
    return g_selected_backend;
}

const char* PCFX_Win32_AudioBackendName(int backend)
{
    switch(backend)
    {
        case PCFX_WIN32_AUDIO_WAVEOUT: return "WaveOut";
#if defined(PCFX_WIN32_HAVE_WASAPI)
        case PCFX_WIN32_AUDIO_WASAPI_SHARED: return "WASAPI shared";
        case PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE: return "WASAPI exclusive";
#endif
        default: return "Unknown";
    }
}

const char* PCFX_Win32_AudioLastError(void)
{
    return g_audio_error[0] ? g_audio_error : "No audio error has been recorded.";
}

uint32_t Audio_Init(void)
{
    Audio_Close();
    audio_clear_error();

    g_active_backend = -1;
    if(g_selected_backend == PCFX_WIN32_AUDIO_WAVEOUT)
    {
        if(waveout_init() != 0)
            return 1;
    }
#if defined(PCFX_WIN32_HAVE_WASAPI)
    else if(g_selected_backend == PCFX_WIN32_AUDIO_WASAPI_SHARED)
    {
        if(wasapi_init(0) != 0)
            return 1;
    }
    else if(g_selected_backend == PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE)
    {
        if(wasapi_init(1) != 0)
            return 1;
    }
#endif
    else
    {
        audio_set_error("Invalid Win32 audio backend id %d.", g_selected_backend);
        return 1;
    }

    g_active_backend = g_selected_backend;
    if(g_audio_muted)
        audio_apply_mute_state();
    return 0;
}

void Audio_Write(int16_t* buffer, uint32_t buffer_size)
{
    if(g_audio_muted)
        return;
    if(g_active_backend == PCFX_WIN32_AUDIO_WAVEOUT)
        waveout_write(buffer, buffer_size);
#if defined(PCFX_WIN32_HAVE_WASAPI)
    else if(g_active_backend == PCFX_WIN32_AUDIO_WASAPI_SHARED ||
            g_active_backend == PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE)
        wasapi_write(buffer, buffer_size);
#endif
}

void PCFX_Win32_AudioSetMuted(int muted)
{
    int new_muted = muted ? 1 : 0;
    if(g_audio_muted == new_muted)
        return;
    g_audio_muted = new_muted;
    audio_apply_mute_state();
}

int PCFX_Win32_AudioGetMuted(void)
{
    return g_audio_muted;
}

void Audio_Close(void)
{
    if(g_active_backend == PCFX_WIN32_AUDIO_WAVEOUT || g_waveout || g_wave_ready)
        waveout_close();
#if defined(PCFX_WIN32_HAVE_WASAPI)
    if(g_active_backend == PCFX_WIN32_AUDIO_WASAPI_SHARED ||
       g_active_backend == PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE ||
       g_wasapi.client || g_wasapi.render)
        wasapi_release();
#endif
    g_active_backend = -1;
}
