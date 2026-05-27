#ifndef SOUND_OUTPUT_H
#define SOUND_OUTPUT_H

#include "shared.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t Audio_Init(void);
void Audio_Write(int16_t* buffer, uint32_t buffer_size);
void Audio_Close(void);

#ifdef __cplusplus
}
#endif

#endif
