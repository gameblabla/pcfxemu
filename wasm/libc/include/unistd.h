#ifndef PCFX_WASM_UNISTD_H
#define PCFX_WASM_UNISTD_H
#define F_OK 0
#define R_OK 4
int access(const char *path, int mode);
#endif
