#ifndef MDFN_SETTINGS_H
#define MDFN_SETTINGS_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint_fast32_t setting_initial_scanline;
extern const uint_fast32_t setting_last_scanline;
extern const uint_fast32_t setting_nospritelimit;
extern const uint_fast32_t setting_rainbow_chromaip;
extern uint_fast32_t setting_cd_speed;
extern uint_fast32_t setting_video_fast_fallback;
void MDFN_SetPCFXFastVideo(uint_fast32_t enabled);
void MDFN_SetPCFXCDSpeed(uint_fast32_t speed);
uint_fast32_t MDFN_GetPCFXCDSpeed(void);

#ifdef __cplusplus
}
#endif

#endif
