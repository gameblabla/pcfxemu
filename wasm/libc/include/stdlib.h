#ifndef PCFX_WASM_STDLIB_H
#define PCFX_WASM_STDLIB_H
#include <stddef.h>
#include <stdint.h>
#ifndef NULL
#define NULL ((void*)0)
#endif
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
void *alloca(size_t size);
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void abort(void) __attribute__((noreturn));
void exit(int status) __attribute__((noreturn));
int atoi(const char *s);
long atol(const char *s);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
int abs(int j);
long labs(long j);
int rand(void);
void srand(unsigned seed);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
#endif
