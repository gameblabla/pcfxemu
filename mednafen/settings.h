#ifndef MDFN_SETTINGS_H
#define MDFN_SETTINGS_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int setting_initial_scanline;
extern int setting_last_scanline;
extern int setting_high_dotclock_width;
extern int setting_nospritelimit;
extern int setting_resamp_quality;
extern int setting_suppress_channel_reset_clicks;
extern int setting_emulate_buggy_codec;
extern int setting_rainbow_chromaip;

// This should assert() or something if the setting isn't found, since it would
// be a totally tubular error!
uint64_t MDFN_GetSettingUI(const char *name);
int64_t MDFN_GetSettingI(const char *name);
double MDFN_GetSettingF(const char *name);
bool MDFN_GetSettingB(const char *name);
const char *MDFN_GetSettingS(const char *name);

#ifdef __cplusplus
}
#endif

#endif
