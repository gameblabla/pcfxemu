#ifndef __PCFX_PCFX_H
#define __PCFX_PCFX_H

#ifdef __cplusplus
#include "../mednafen.h"
#include "../state.h"
#include "../general.h"
#include "v810/v810_cpu.h"
#include "../hw_video/huc6270/vdc.h"
#else
#include "../mednafen.h"
#include "../state.h"
#include "../settings.h"
#include "v810/v810_cpu.h"
#include "../hw_video/huc6270/vdc.h"
#endif

#define PCFX_MASTER_CLOCK	21477272.72

#if 0
 #define FXDBG(format, ...) MDFN_DebugPrint(format, ## __VA_ARGS__)
#elif defined(_WIN32)
static inline void FXDBG(const char *format, ...) { (void)0; }
#else
 #define FXDBG(format, ...) ((void)0)
#endif

static inline void MDFN_FastU32MemsetM8(uint32_t *array, uint32_t value_32, unsigned int u32len)
{
   uint32_t *ai;

   for(ai = array; ai < array + u32len; ai += 2)
   {
      ai[0] = value_32;
      ai[1] = value_32;
   }
}


#ifdef __cplusplus
extern "C" {
#endif

int32 MDFN_FASTCALL pcfx_event_handler(const v810_timestamp_t timestamp);

void ForceEventUpdates(const uint32 timestamp);

extern VDC *fx_vdc_chips[2];

#define REGSETHW(_reg, _data, _msh) { _reg &= 0xFFFF << (_msh ? 0 : 16); _reg |= _data << (_msh ? 16 : 0); }
#define REGGETHW(_reg, _msh) ((_reg >> (_msh ? 16 : 0)) & 0xFFFF)

enum
{
 PCFX_EVENT_PAD = 0,
 PCFX_EVENT_TIMER,
 PCFX_EVENT_KING,
 PCFX_EVENT_ADPCM
};

#define PCFX_EVENT_NONONO       0x7fffffff

void PCFX_SetEvent(const int type, const v810_timestamp_t next_timestamp);
int PCFX_SwapCD(const char *path);
#ifdef HAVE_HUC6273
bool PCFX_HuC6273Active(void);
#else
static inline bool PCFX_HuC6273Active(void) { return false; }
#endif
void PCFX_SetControllerType(uint8_t type);
uint8_t PCFX_GetControllerType(void);
void PCFX_SoftReset(void);



#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
