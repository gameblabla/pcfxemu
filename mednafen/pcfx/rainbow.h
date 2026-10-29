#ifndef __PCFX_RAINBOW_H
#define __PCFX_RAINBOW_H

#include <stdbool.h>
#include "pcfx.h"

#ifdef __cplusplus
extern "C" {
#endif

void RAINBOW_Write8(uint32 A, uint8 V);
void RAINBOW_Write16(uint32 A, uint16 V);
void RAINBOW_ForceTransferReset(void);
void RAINBOW_SwapBuffers(void);
void RAINBOW_DecodeBlock(bool arg_FirstDecode, bool Skip);
/* Token-by-token, grant-fed RLE; JPEG remains block-staged. */
void RAINBOW_RLEBegin(bool first);
void RAINBOW_RLEEmit(unsigned x, unsigned count, uint8 color);
void RAINBOW_RLEEnd(void);
int RAINBOW_FetchRaster(uint32 *, uint32 layer_or, uint32 *palette_ptr);
int RAINBOW_StateAction(StateMem *sm, int load, int data_only);
bool RAINBOW_Init(bool arg_ChromaIP);
void RAINBOW_Close(void);
void RAINBOW_Reset(void);
bool RAINBOW_IsFastBackend(void);
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff);

#ifdef __cplusplus
}
#endif

#endif
