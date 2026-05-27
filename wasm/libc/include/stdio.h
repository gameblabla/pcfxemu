#ifndef PCFX_WASM_STDIO_H
#define PCFX_WASM_STDIO_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#ifndef NULL
#define NULL ((void*)0)
#endif
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
typedef struct PCFX_WASM_FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *fp);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp);
int fseek(FILE *fp, long offset, int whence);
long ftell(FILE *fp);
int fseeko(FILE *fp, long offset, int whence);
long ftello(FILE *fp);
int fflush(FILE *fp);
int ferror(FILE *fp);
void clearerr(FILE *fp);
int feof(FILE *fp);
int fgetc(FILE *fp);
int getc(FILE *fp);
int ungetc(int c, FILE *fp);
char *fgets(char *s, int size, FILE *fp);
int fputc(int c, FILE *fp);
int fputs(const char *s, FILE *fp);
int putc(int c, FILE *fp);
int puts(const char *s);
int printf(const char *fmt, ...);
int fprintf(FILE *fp, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *fp, const char *fmt, va_list ap);
int vsprintf(char *str, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int sscanf(const char *str, const char *fmt, ...);
int vsscanf(const char *str, const char *fmt, va_list ap);
int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
#endif
