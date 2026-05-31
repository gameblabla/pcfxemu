#ifndef PCFX_WIN32_SOUND_OUTPUT_H
#define PCFX_WIN32_SOUND_OUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    PCFX_WIN32_AUDIO_WAVEOUT = 0,
    PCFX_WIN32_AUDIO_WASAPI_SHARED = 1,
    PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE = 2
};

void PCFX_Win32_AudioSetBackend(int backend);
int  PCFX_Win32_AudioGetBackend(void);
const char* PCFX_Win32_AudioBackendName(int backend);
const char* PCFX_Win32_AudioLastError(void);
void PCFX_Win32_AudioSetMuted(int muted);
int  PCFX_Win32_AudioGetMuted(void);

#ifdef __cplusplus
}
#endif

#endif
