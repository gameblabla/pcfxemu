#ifndef PCFX_WASM_SYS_STAT_H
#define PCFX_WASM_SYS_STAT_H
#include <sys/types.h>
#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
struct stat { unsigned int st_mode; long st_size; };
int stat(const char *path, struct stat *st);
int mkdir(const char *path, mode_t mode);
#endif
