#ifndef __PCFX_RAINBOW_BACKEND_H
#define __PCFX_RAINBOW_BACKEND_H

#include <stdbool.h>
#include "pcfx.h"

#ifdef __cplusplus
extern "C" {
#endif

void RAINBOW_Fast_Write8(uint32 A, uint8 V);
void RAINBOW_Fast_Write16(uint32 A, uint16 V);
void RAINBOW_Fast_ForceTransferReset(void);
void RAINBOW_Fast_SwapBuffers(void);
void RAINBOW_Fast_DecodeBlock(bool arg_FirstDecode, bool Skip);
void RAINBOW_Fast_RLEBegin(bool first);
void RAINBOW_Fast_RLEEmit(unsigned x, unsigned count, uint8 color);
void RAINBOW_Fast_RLEEnd(void);
void RAINBOW_Fast_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff);
int RAINBOW_Fast_FetchRaster(uint32 *linebuffer, uint32 layer_or, uint32 *palette_ptr);
int RAINBOW_Fast_StateAction(StateMem *sm, int load, int data_only);
bool RAINBOW_Fast_Init(bool arg_ChromaIP);
void RAINBOW_Fast_Close(void);
void RAINBOW_Fast_Reset(void);

void RAINBOW_Accurate_Write8(uint32 A, uint8 V);
void RAINBOW_Accurate_Write16(uint32 A, uint16 V);
void RAINBOW_Accurate_ForceTransferReset(void);
void RAINBOW_Accurate_SwapBuffers(void);
void RAINBOW_Accurate_DecodeBlock(bool arg_FirstDecode, bool Skip);
void RAINBOW_Accurate_RLEBegin(bool first);
void RAINBOW_Accurate_RLEEmit(unsigned x, unsigned count, uint8 color);
void RAINBOW_Accurate_RLEEnd(void);
void RAINBOW_Accurate_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff);
int RAINBOW_Accurate_FetchRaster(uint32 *linebuffer, uint32 layer_or, uint32 *palette_ptr);
int RAINBOW_Accurate_StateAction(StateMem *sm, int load, int data_only);
bool RAINBOW_Accurate_Init(bool arg_ChromaIP);
void RAINBOW_Accurate_Close(void);
void RAINBOW_Accurate_Reset(void);

#ifdef __cplusplus
}
#endif

#endif
