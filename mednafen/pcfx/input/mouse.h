#ifndef __PCFX_INPUT_MOUSE_H
#define __PCFX_INPUT_MOUSE_H

#include "../input.h"

#ifdef __cplusplus
extern "C" {
extern const InputDeviceInputInfoStruct PCFX_MouseIDII[4];
#endif

PCFX_Input_Device *PCFXINPUT_MakeMouse(int which);

#ifdef __cplusplus
}
#endif

#endif
