/* C11 V810 floating point helpers. */
#ifndef V810_FP_OPS_H
#define V810_FP_OPS_H
#include "mednafen/mednafen-types.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
 V810_FP_FLAG_INVALID = 0x0001,
 V810_FP_FLAG_DIVBYZERO = 0x0002,
 V810_FP_FLAG_OVERFLOW = 0x0004,
 V810_FP_FLAG_UNDERFLOW = 0x0008,
 V810_FP_FLAG_INEXACT = 0x0010,
 V810_FP_FLAG_RESERVED = 0x0020
};

uint32 V810_FP_mul(uint32 a, uint32 b);
uint32 V810_FP_div(uint32 a, uint32 b);
uint32 V810_FP_add(uint32 a, uint32 b);
uint32 V810_FP_sub(uint32 a, uint32 b);
int V810_FP_cmp(uint32 a, uint32 b);
uint32 V810_FP_itof(uint32 v);
uint32 V810_FP_ftoi(uint32 v, bool truncate);
uint32 V810_FP_get_flags(void);
void V810_FP_clear_flags(void);

#ifdef __cplusplus
}
#endif
#endif
