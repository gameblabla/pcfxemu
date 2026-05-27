#ifndef PCFX_WASM_TIME_H
#define PCFX_WASM_TIME_H
typedef long time_t;
typedef long clock_t;
struct tm { int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst; };
time_t time(time_t *tloc);
#endif
