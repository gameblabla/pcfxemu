#ifndef __MDFN_FXSCSI_H
#define __MDFN_FXSCSI_H

#include <boolean.h>
#include "../mednafen-types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool FXSCSI_Init(void);
uint8 FXSCSI_CtrlRead(uint32 A);
void FXSCSI_CtrlWrite(uint32 A, uint8 V);

#ifdef __cplusplus
}
#endif

#endif
