#ifndef __PCFX_INPUT_GAMEPAD_H
#define __PCFX_INPUT_GAMEPAD_H

#include "../input.h"

#ifdef __cplusplus
extern "C" {
extern const InputDeviceInputInfoStruct PCFX_GamepadIDII[0xF];
extern const InputDeviceInputInfoStruct PCFX_GamepadIDII_DSR[0xF];
#endif

PCFX_Input_Device *PCFXINPUT_MakeGamepad(int which);

#ifdef __cplusplus
}
#endif

#endif
