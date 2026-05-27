#ifndef __PCFX_HUC6273_H
#define __PCFX_HUC6273_H

#include "pcfx/pcfx.h"

#ifdef __cplusplus
extern "C" {
#endif

bool HuC6273_Init(void);

uint8 HuC6273_Read8(uint32 A);
uint16 HuC6273_Read16(uint32 A);
void HuC6273_Write16(uint32 A, uint16 V);
void HuC6273_Write32(uint32 A, uint32 V);
void HuC6273_Write8(uint32 A, uint8 V);
void HuC6273_Reset(void);
void HuC6273_FrameBoundary(void);
int HuC6273_StateAction(StateMem *sm, int load, int data_only);
bool HuC6273_LineHasPixels(int y);
void HuC6273_RenderLine(MDFN_Pixel *target, int y, int width);
void HuC6273_RenderLinePriority(MDFN_Pixel *target, int y, int width, const uint8 *vce_top_prio, uint8 aurora_prio);

#ifdef __cplusplus
}
#endif

#endif
