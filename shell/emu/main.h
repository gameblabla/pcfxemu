#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PCFX_BIOS_PATCH_SHORTINTRO 0x01u
#define PCFX_BIOS_PATCH_ENGLISH 0x02u
#define PCFX_BIOS_PATCH_AUTOLAUNCH 0x04u

#define PCFX_ADPCM_BUGGY_AUTO 0
#define PCFX_ADPCM_BUGGY_OFF  1
#define PCFX_ADPCM_BUGGY_ON   2

#ifdef __cplusplus
extern "C" {
#endif

bool SaveState(char* path, uint_fast8_t state);
void SRAM_Save(char* path, uint_fast8_t state);
void Emu_Init(void);
int Load_Game_Memory(char* path);
int Load_BIOS_Memory(void);
void Emulation_Run(void);
void PCFX_CoreClose(void);
void PCFX_SoftReset(void);
void PCFX_SetSystemMode(int mode);
void PCFX_SetPreferFXGABIOS(bool enabled);
void PCFX_SetBIOSPatches(uint32_t flags);
uint32_t PCFX_GetBIOSPatches(void);
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
void PCFX_SetADPCMOptions(bool emulate_buggy_codec, bool suppress_channel_reset_clicks);
void PCFX_SetADPCMCompatOptions(int buggy_codec_mode, bool suppress_channel_reset_clicks);
int PCFX_GetADPCMBuggyCodecMode(void);
bool PCFX_GetADPCMEmulateBuggyCodec(void);
bool PCFX_GetADPCMSuppressChannelResetClicks(void);
#endif
void PCFX_SetCDSpeed(uint_fast32_t speed);
uint_fast32_t PCFX_GetCDSpeed(void);
#ifdef HAVE_HUC6273
void PCFX_SetHuC6273Enabled(bool enabled);
bool PCFX_GetHuC6273Enabled(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
