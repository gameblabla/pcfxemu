#ifndef PCFX_WASM_STRINGS_H
#define PCFX_WASM_STRINGS_H
#include <stddef.h>
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
void bzero(void *s, size_t n);
#endif
