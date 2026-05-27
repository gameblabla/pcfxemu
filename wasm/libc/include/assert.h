#ifndef PCFX_WASM_ASSERT_H
#define PCFX_WASM_ASSERT_H
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
void abort(void);
#define assert(x) ((x) ? (void)0 : abort())
#endif
#endif
