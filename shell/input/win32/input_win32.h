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

const char* PCFX_Win32_InputButtonName(int button);
void PCFX_Win32_InputDefaults(void);
void PCFX_Win32_InputSetMapping(int player, int button, uint32_t vk);
uint32_t PCFX_Win32_InputGetMapping(int player, int button);
void PCFX_Win32_InputVKName(uint32_t vk, char* out, unsigned out_size);
void PCFX_Win32_InputSetWindow(HWND hwnd);
void PCFX_Win32_InputResetMouse(void);

/* Gamepad input backend.
 *
 * The 64-bit build uses SDL3 (which natively supports XInput/DirectInput/HID
 * controllers); the 32-bit XP-compatible build uses the WinMM joystick API,
 * since modern SDL3 does not run on Windows XP. Either way controllers use a
 * fixed sensible default mapping onto the PC-FX pad, so there is no per-button
 * gamepad remapping UI -- only a single enable toggle. Keyboard remapping is
 * unchanged. */
void PCFX_Win32_InputSetGamepadEnabled(int enabled);
int  PCFX_Win32_InputGetGamepadEnabled(void);
const char* PCFX_Win32_InputGamepadBackendName(void);

int PCFX_Win32_InputButtonDown(int player, int button);

#ifdef __cplusplus
}
#endif

#endif
