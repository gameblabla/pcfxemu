#ifndef __MDFN_PSX_MASMEM_H
#define __MDFN_PSX_MASMEM_H

// TODO, WIP (big-endian stores and loads not fully supported yet)

#ifdef MSB_FIRST
#define MAS_NATIVE_IS_BIGENDIAN 1
#else
#define MAS_NATIVE_IS_BIGENDIAN 0
#endif

static INLINE uint16 LoadU16_RBO(const uint16 *a)
{
 #ifdef ARCH_POWERPC
  uint16 tmp;

  __asm__ ("lhbrx %0, %y1" : "=r"(tmp) : "Z"(*a));

  return(tmp);

 #else
  uint16 tmp = *a;
  return((tmp << 8) | (tmp >> 8));
 #endif
}

static INLINE uint16 LoadU16_LE(const uint16 *a)
{
#ifdef MSB_FIRST
   return LoadU16_RBO(a);
#else
   return *a;
#endif
}

#undef MAS_NATIVE_IS_BIGENDIAN

#endif
