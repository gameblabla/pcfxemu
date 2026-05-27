#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool SaveState(char* path, uint_fast8_t state);
void SRAM_Save(char* path, uint_fast8_t state);

#ifdef __cplusplus
}
#endif

#endif
