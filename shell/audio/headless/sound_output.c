#include <stdint.h>
#include "sound_output.h"

void pcfx_headless_audio_write(const int16_t* samples, uint32_t frames) __attribute__((weak));

uint32_t Audio_Init(void)
{
    return 0;
}

void Audio_Write(int16_t* buffer, uint32_t buffer_size)
{
    if(pcfx_headless_audio_write)
        pcfx_headless_audio_write(buffer, buffer_size);
}

void Audio_Close(void)
{
}
