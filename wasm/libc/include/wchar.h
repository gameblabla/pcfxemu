#ifndef PCFX_WASM_WCHAR_H
#define PCFX_WASM_WCHAR_H
#include <stddef.h>
typedef int wchar_t;
typedef struct { int __dummy; } mbstate_t;
size_t wcsrtombs(char *dest, const wchar_t **src, size_t len, mbstate_t *ps);
#endif
