#ifndef PCFX_WASM_SYS_TIME_H
#define PCFX_WASM_SYS_TIME_H
#include <time.h>
struct timeval { long tv_sec; long tv_usec; };
struct timezone { int tz_minuteswest; int tz_dsttime; };
int gettimeofday(struct timeval *tv, struct timezone *tz);
#endif
