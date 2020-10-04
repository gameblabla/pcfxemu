#ifndef __MDFN_JREVDCT_H
#define __MDFN_JREVDCT_H

#include "../mednafen-types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32* DCTBLOCK;     
typedef int32 DCTELEM;

void j_rev_dct(DCTBLOCK data);

#ifdef __cplusplus
}
#endif

#endif
