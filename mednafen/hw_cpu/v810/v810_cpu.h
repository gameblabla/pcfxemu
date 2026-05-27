////////////////////////////////////////////////////////////////
// C11 V810 CPU public API
#ifndef V810_CPU_H_
#define V810_CPU_H_

#include "v810_fp_ops.h"
#include "mednafen/state.h"

typedef int32 v810_timestamp_t;

#define V810_FAST_MAP_SHIFT 16
#define V810_FAST_MAP_PSIZE (1 << V810_FAST_MAP_SHIFT)
#define V810_FAST_MAP_TRAMPOLINE_SIZE 1024

enum { ECODE_TRAP_BASE = 0xFFA0, ECODE_INVALID_OP = 0xFF90, ECODE_ZERO_DIV = 0xFF80, ECODE_FIV = 0xFF70, ECODE_FZD = 0xFF68, ECODE_FOV = 0xFF64, ECODE_FRO = 0xFF60 };
enum { INVALID_OP_HANDLER_ADDR = 0xFFFFFF90, ZERO_DIV_HANDLER_ADDR = 0xFFFFFF80, FPU_HANDLER_ADDR = 0xFFFFFF60, TRAP_HANDLER_BASE = 0xFFFFFFA0 };

#define EIPC 0
#define EIPSW 1
#define FEPC 2
#define FEPSW 3
#define ECR 4
#define PSW 5
#define PIR 6
#define TKCW 7
#define CHCW 24
#define ADDTRE 25
#define PSW_IA 0xF0000
#define PSW_I3 0x80000
#define PSW_I2 0x40000
#define PSW_I1 0x20000
#define PSW_I0 0x10000
#define PSW_NP 0x08000
#define PSW_EP 0x04000
#define PSW_AE 0x02000
#define PSW_ID 0x01000
#define PSW_FRO 0x00200
#define PSW_FIV 0x00100
#define PSW_FZD 0x00080
#define PSW_FOV 0x00040
#define PSW_FUD 0x00020
#define PSW_FPR 0x00010
#define PSW_CY 0x00008
#define PSW_OV 0x00004
#define PSW_S 0x00002
#define PSW_Z 0x00001
#define COND_V 0
#define COND_C 1
#define COND_Z 2
#define COND_NH 3
#define COND_S 4
#define COND_T 5
#define COND_LT 6
#define COND_LE 7
#define COND_NV 8
#define COND_NC 9
#define COND_NZ 10
#define COND_H 11
#define COND_NS 12
#define COND_F 13
#define COND_GE 14
#define COND_GT 15
#define TESTCOND_V (S_REG[PSW]&PSW_OV)
#define TESTCOND_L (S_REG[PSW]&PSW_CY)
#define TESTCOND_C TESTCOND_L
#define TESTCOND_E (S_REG[PSW]&PSW_Z)
#define TESTCOND_Z TESTCOND_E
#define TESTCOND_NH ((S_REG[PSW]&PSW_Z) || (S_REG[PSW]&PSW_CY))
#define TESTCOND_N (S_REG[PSW]&PSW_S)
#define TESTCOND_S TESTCOND_N
#define TESTCOND_LT ((!!(S_REG[PSW]&PSW_S)) ^ (!!(S_REG[PSW]&PSW_OV)))
#define TESTCOND_LE (((!!(S_REG[PSW]&PSW_S)) ^ (!!(S_REG[PSW]&PSW_OV))) || (S_REG[PSW]&PSW_Z))
#define TESTCOND_NV (!(S_REG[PSW]&PSW_OV))
#define TESTCOND_NL (!(S_REG[PSW]&PSW_CY))
#define TESTCOND_NC TESTCOND_NL
#define TESTCOND_NE (!(S_REG[PSW]&PSW_Z))
#define TESTCOND_NZ TESTCOND_NE
#define TESTCOND_H (!((S_REG[PSW]&PSW_Z) || (S_REG[PSW]&PSW_CY)))
#define TESTCOND_P (!(S_REG[PSW] & PSW_S))
#define TESTCOND_NS TESTCOND_P
#define TESTCOND_GE (!((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV))))
#define TESTCOND_GT (!(((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV))) || (S_REG[PSW]&PSW_Z)))

typedef enum { V810_EMU_MODE_FAST = 0, V810_EMU_MODE_ACCURATE = 1 } V810_Emu_Mode;
#if defined(PCFX_V810_ACCURATE_ONLY) && defined(PCFX_V810_FAST_ONLY)
#error "Select only one V810 core"
#endif
#if !defined(PCFX_V810_ACCURATE_ONLY) && !defined(PCFX_V810_FAST_ONLY)
#define PCFX_V810_ACCURATE_ONLY 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern v810_timestamp_t v810_timestamp;
extern v810_timestamp_t next_event_ts;

bool V810_Init(void);
const char* V810_GetCoreName(void);
void V810_Kill(void);
void V810_SetInt(int level);
void V810_SetMemWriteBus32(uint8 A, bool value);
void V810_SetMemReadBus32(uint8 A, bool value);
void V810_SetMemReadHandlers(uint8 MDFN_FASTCALL (*read8)(v810_timestamp_t *, uint32), uint16 MDFN_FASTCALL (*read16)(v810_timestamp_t *, uint32), uint32 MDFN_FASTCALL (*read32)(v810_timestamp_t *, uint32));
void V810_SetMemWriteHandlers(void MDFN_FASTCALL (*write8)(v810_timestamp_t *, uint32, uint8), void MDFN_FASTCALL (*write16)(v810_timestamp_t *, uint32, uint16), void MDFN_FASTCALL (*write32)(v810_timestamp_t *, uint32, uint32));
void V810_SetIOReadHandlers(uint8 MDFN_FASTCALL (*read8)(v810_timestamp_t *, uint32), uint16 MDFN_FASTCALL (*read16)(v810_timestamp_t *, uint32), uint32 MDFN_FASTCALL (*read32)(v810_timestamp_t *, uint32));
void V810_SetIOWriteHandlers(void MDFN_FASTCALL (*write8)(v810_timestamp_t *, uint32, uint8), void MDFN_FASTCALL (*write16)(v810_timestamp_t *, uint32, uint16), void MDFN_FASTCALL (*write32)(v810_timestamp_t *, uint32, uint32));
uint8 *V810_SetFastMap(uint32 addresses[], uint32 length, unsigned int num_addresses);
static inline void V810_ResetTS(v810_timestamp_t new_base_timestamp) { next_event_ts -= (v810_timestamp - new_base_timestamp); v810_timestamp = new_base_timestamp; }
static inline void V810_SetEventNT(v810_timestamp_t timestamp) { next_event_ts = timestamp; }
static inline v810_timestamp_t V810_GetEventNT(void) { return next_event_ts; }
v810_timestamp_t V810_Run(int32 MDFN_FASTCALL (*event_handler)(const v810_timestamp_t timestamp));
void V810_Exit(void);
void V810_Reset(void);
int V810_StateAction(StateMem *sm, int load, int data_only);
uint32 V810_GetPC(void);
void V810_SetPC(uint32);
uint32 V810_GetPR(const unsigned int which);
void V810_SetPR(const unsigned int which, uint32 value);
typedef bool (*V810_EmuTrapHandler)(uint32 trap_id);
void V810_SetEmuTrapHandler(V810_EmuTrapHandler handler);
uint32 V810_GetSR(const unsigned int which);

#ifdef __cplusplus
}
#endif
#endif
