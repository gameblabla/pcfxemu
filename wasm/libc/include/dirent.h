#ifndef PCFX_WASM_DIRENT_H
#define PCFX_WASM_DIRENT_H
typedef struct DIR DIR;
struct dirent { char d_name[256]; };
DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
#endif
