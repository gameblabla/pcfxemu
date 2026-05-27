#ifndef __MDFN_CLAMP_H
#define __MDFN_CLAMP_H

#include <stddef.h>
#include <stdint.h>
#include "mednafen-types.h"

static inline int32 clamp_to_u8(int32 i)
{
 if(i & 0xFFFFFF00)
  i = (((~i) >> 30) & 0xFF);
 return(i);
}


#endif
