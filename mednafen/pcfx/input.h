#ifndef __PCFX_PAD_H
#define __PCFX_PAD_H

#include <stdbool.h>
#include "../mednafen-types.h"
#include "../state.h"
#include "v810/v810_cpu.h"
#ifdef __cplusplus
#include "../git.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum
{
 FX_SIG_MOUSE = 0xD,
 FX_SIG_TAP = 0xE,
 FX_SIG_PAD = 0xF
};

typedef struct PCFX_Input_Device PCFX_Input_Device;

uint32 PCFX_Input_Device_ReadTransferTime(PCFX_Input_Device *dev);
uint32 PCFX_Input_Device_WriteTransferTime(PCFX_Input_Device *dev);
uint32 PCFX_Input_Device_Read(PCFX_Input_Device *dev);
void PCFX_Input_Device_Write(PCFX_Input_Device *dev, uint32 data);
void PCFX_Input_Device_Power(PCFX_Input_Device *dev);
void PCFX_Input_Device_Frame(PCFX_Input_Device *dev, const void *data);
int PCFX_Input_Device_StateAction(PCFX_Input_Device *dev, StateMem *sm, int load, int data_only, const char *section_name);
void PCFX_Input_Device_Destroy(PCFX_Input_Device *dev);
PCFX_Input_Device *PCFX_Input_Device_CreateNone(void);

void FXINPUT_Init(void);
void FXINPUT_Kill(void);
void FXINPUT_SettingChanged(void);

void FXINPUT_SetInput(int port, uint_fast8_t type, void *ptr);

uint16 FXINPUT_Read16(uint32 A, const v810_timestamp_t timestamp);
uint8 FXINPUT_Read8(uint32 A, const v810_timestamp_t timestamp);

void FXINPUT_Write8(uint32 A, uint8 V, const v810_timestamp_t timestamp);
void FXINPUT_Write16(uint32 A, uint16 V, const v810_timestamp_t timestamp);

void FXINPUT_Frame(void);
int FXINPUT_StateAction(StateMem *sm, int load, int data_only);

v810_timestamp_t FXINPUT_Update(const v810_timestamp_t timestamp);
void FXINPUT_ResetTS(int32 ts_base);

#ifdef __cplusplus
extern InputInfoStruct PCFXInputInfo;
#endif

#ifdef __cplusplus
} /* extern \"C\" */
#endif

#endif
