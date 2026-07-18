#ifndef PCFX_WIN32_SDL3_INPUT_H
#define PCFX_WIN32_SDL3_INPUT_H

/* SDL3 gamepad input for the 64-bit Win32 (Win64) PCFXEmu frontend.
 *
 * The native frontend keeps its own window, D3D11/GDI video and WASAPI/waveOut
 * audio; SDL3 is used only as an additional gamepad source, OR-combined with
 * the existing keyboard and XInput paths in shell/input/win32/input_emu.c. This
 * mirrors how GP32emu ships a native Win64 frontend that uses SDL3 purely for
 * joystick input.
 *
 * Everything here is compiled only when PCFX_WIN32_HAVE_SDL3 is defined
 * (the SDL3=YES win64 build of Makefile.win32). The 32-bit legacy build is
 * unaffected and stays keyboard + XInput only.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the SDL3 gamepad subsystem and open any already-connected pads.
 * Returns 1 when SDL3 gamepad input is available, 0 otherwise. Safe to call
 * more than once; a failed init leaves every other entry point a no-op. */
int  PCFX_Win32_SDL3_Init(void);

/* Release open gamepads and shut the SDL3 subsystem down. */
void PCFX_Win32_SDL3_Shutdown(void);

/* Pump SDL events (handling gamepad hotplug) and refresh gamepad state.
 * Call once per input frame before reading pad buttons. */
void PCFX_Win32_SDL3_Poll(void);

/* Combined PC-FX pad button bitmask for a player, in the same bit layout the
 * keyboard/XInput path uses (Up=0x100, Down=0x400, Left=0x800, Right=0x200,
 * I=0x1..VI=0x20, Run=0x80, Select=0x40). Returns 0 when no pad is assigned. */
uint16_t PCFX_Win32_SDL3_PadButtons(unsigned player);

/* 1 if SDL delivered a quit request (e.g. via an SDL_EVENT_QUIT), else 0.
 * The native window normally owns shutdown, so this is a fallback only. */
int  PCFX_Win32_SDL3_QuitRequested(void);

/* One-line status string suitable for the About box / log. */
const char *PCFX_Win32_SDL3_Status(void);

#ifdef __cplusplus
}
#endif

#endif /* PCFX_WIN32_SDL3_INPUT_H */
