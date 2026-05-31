#ifndef PCFX_WIN32_INPUT_H
#define PCFX_WIN32_INPUT_H

#include <stdint.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCFX_WIN32_PLAYERS 2
#define PCFX_WIN32_BUTTONS 14
#define PCFX_WIN32_XINPUT_USERS 4

const char* PCFX_Win32_InputButtonName(int button);
void PCFX_Win32_InputDefaults(void);
void PCFX_Win32_InputSetMapping(int player, int button, uint32_t vk);
uint32_t PCFX_Win32_InputGetMapping(int player, int button);
void PCFX_Win32_InputVKName(uint32_t vk, char* out, unsigned out_size);
void PCFX_Win32_InputSetWindow(HWND hwnd);
void PCFX_Win32_InputResetMouse(void);

void PCFX_Win32_InputSetXInputEnabled(int enabled);
int PCFX_Win32_InputGetXInputEnabled(void);
void PCFX_Win32_InputSetXInputUser(int player, int user_index);
int PCFX_Win32_InputGetXInputUser(int player);
void PCFX_Win32_InputSetXInputMapping(int player, int button, uint32_t code);
uint32_t PCFX_Win32_InputGetXInputMapping(int player, int button);
void PCFX_Win32_InputXInputName(uint32_t code, char* out, unsigned out_size);
int PCFX_Win32_InputPollXInputCapture(int player, uint32_t* code_out);
int PCFX_Win32_InputButtonDown(int player, int button);
int PCFX_Win32_InputXInputCodeDown(int player, uint32_t code);

#ifdef __cplusplus
}
#endif

#endif
