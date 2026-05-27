#ifndef PCFX_WASM_MATH_H
#define PCFX_WASM_MATH_H
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
double sin(double); double cos(double); double tan(double); double asin(double); double acos(double); double atan(double); double atan2(double,double); double sqrt(double); double pow(double,double); double exp(double); double log(double); double log10(double); double floor(double); double ceil(double); double fabs(double); double fmod(double,double); double round(double); long lround(double); long lrint(double);
float sinf(float); float cosf(float); float tanf(float); float sqrtf(float); float powf(float,float); float floorf(float); float ceilf(float); float fabsf(float); float fmodf(float,float); float roundf(float); long lroundf(float); long lrintf(float);
#endif
