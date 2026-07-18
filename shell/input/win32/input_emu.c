#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "input_emu.h"
#include "input_win32.h"
#include "config.h"
#ifdef PCFX_WIN32_HAVE_SDL3
#include "pcfx_sdl3_input.h"
#else
#include <mmsystem.h>   /* WinMM joystick API (32-bit / XP-compatible build) */
#endif

extern uint8_t exit_vb;
extern uint32_t emulator_state;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

static HWND g_hwnd;
static POINT g_prev_mouse;
static int g_mouse_valid;
static int32_t g_mouse_dx;
static int32_t g_mouse_dy;

static uint32_t g_keymap[PCFX_WIN32_PLAYERS][PCFX_WIN32_BUTTONS];
static int g_gamepad_enabled = 1;

static const char* const g_button_names[PCFX_WIN32_BUTTONS] = {
    "Up", "Down", "Left", "Right",
    "I / A", "II / B", "III / C", "IV / X", "V / Y", "VI / Z",
    "Run / Start", "Select", "Mouse Left", "Mouse Right"
};

/* PC-FX pad bit for pad-button index 0..11 (Up, Down, Left, Right, I..VI,
 * Run, Select). */
static const uint16_t g_button_bits[12] = {
    256, 1024, 2048, 512,
    1, 2, 4, 8, 16, 32,
    128, 64
};

/* Individual pad-button bits, for readability in the gamepad backends. */
#define PADB_UP      0x0100u
#define PADB_DOWN    0x0400u
#define PADB_LEFT    0x0800u
#define PADB_RIGHT   0x0200u
#define PADB_I       0x0001u
#define PADB_II      0x0002u
#define PADB_III     0x0004u
#define PADB_IV      0x0008u
#define PADB_V       0x0010u
#define PADB_VI      0x0020u
#define PADB_RUN     0x0080u
#define PADB_SELECT  0x0040u

/* -------------------------------------------------------------------------
 * Gamepad backend
 *
 * SDL3 on the 64-bit build (it already covers XInput/DirectInput/HID pads);
 * WinMM joyGetPosEx on the 32-bit XP-compatible build. Both hand back a PC-FX
 * pad bitmask (g_button_bits layout) using a fixed default mapping.
 * ------------------------------------------------------------------------- */

#ifdef PCFX_WIN32_HAVE_SDL3

static void gamepad_poll(void)
{
    if(!g_gamepad_enabled)
        return;
    PCFX_Win32_SDL3_Poll();
}

static uint16_t gamepad_pad_mask(unsigned player)
{
    if(!g_gamepad_enabled || player >= PCFX_WIN32_PLAYERS)
        return 0;
    return PCFX_Win32_SDL3_PadButtons(player);
}

const char* PCFX_Win32_InputGamepadBackendName(void)
{
    return "SDL3";
}

#else /* WinMM joystick backend */

#define WINMM_AXIS_DEADZONE_FRAC 3   /* fraction of half-range treated as dead */

static int winmm_map_pov(DWORD pov)
{
    /* dwPOV is in hundredths of a degree, clockwise from "up"; 0xFFFF center. */
    uint16_t mask = 0;
    if(pov == JOY_POVCENTERED || pov == 0xFFFF)
        return 0;
    if(pov > 27000 || pov < 9000)   mask |= PADB_UP;
    if(pov > 0     && pov < 18000)  mask |= PADB_RIGHT;
    if(pov > 9000  && pov < 27000)  mask |= PADB_DOWN;
    if(pov > 18000)                 mask |= PADB_LEFT;
    return mask;
}

static uint16_t winmm_pad_mask(unsigned player)
{
    UINT id = JOYSTICKID1 + player;

    JOYCAPS caps;
    if(joyGetDevCaps(id, &caps, sizeof(caps)) != JOYERR_NOERROR)
        return 0;

    JOYINFOEX ji;
    memset(&ji, 0, sizeof(ji));
    ji.dwSize = sizeof(ji);
    ji.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS | JOY_RETURNPOV;
    if(joyGetPosEx(id, &ji) != JOYERR_NOERROR)
        return 0;

    uint16_t mask = 0;

    /* Face buttons -> I..VI, then Select / Run. WinMM button order is
     * device-dependent; this default matches the common pad layout. */
    static const uint16_t btn_to_pad[8] = {
        PADB_I, PADB_II, PADB_III, PADB_IV, PADB_V, PADB_VI, PADB_SELECT, PADB_RUN
    };
    for(int i = 0; i < 8; i++)
        if(ji.dwButtons & (1u << i))
            mask |= btn_to_pad[i];

    /* Hat switch. */
    mask |= winmm_map_pov(ji.dwPOV);

    /* Left stick / D-pad reported as the primary X/Y axes. */
    DWORD xmin = caps.wXmin, xmax = caps.wXmax;
    DWORD ymin = caps.wYmin, ymax = caps.wYmax;
    if(xmax > xmin)
    {
        DWORD xcenter = xmin + (xmax - xmin) / 2;
        DWORD xdead   = (xmax - xmin) / (2 * WINMM_AXIS_DEADZONE_FRAC);
        if(ji.dwXpos + xdead < xcenter) mask |= PADB_LEFT;
        if(ji.dwXpos > xcenter + xdead) mask |= PADB_RIGHT;
    }
    if(ymax > ymin)
    {
        DWORD ycenter = ymin + (ymax - ymin) / 2;
        DWORD ydead   = (ymax - ymin) / (2 * WINMM_AXIS_DEADZONE_FRAC);
        if(ji.dwYpos + ydead < ycenter) mask |= PADB_UP;
        if(ji.dwYpos > ycenter + ydead) mask |= PADB_DOWN;
    }

    return mask;
}

static void gamepad_poll(void)
{
    /* WinMM is polled on demand in winmm_pad_mask(); nothing to pump here. */
}

static uint16_t gamepad_pad_mask(unsigned player)
{
    if(!g_gamepad_enabled || player >= PCFX_WIN32_PLAYERS)
        return 0;
    return winmm_pad_mask(player);
}

const char* PCFX_Win32_InputGamepadBackendName(void)
{
    return "WinMM joystick";
}

#endif /* PCFX_WIN32_HAVE_SDL3 */

static int gamepad_button_down(unsigned player, int button)
{
    if(button < 0 || button >= 12)
        return 0;
    return (gamepad_pad_mask(player) & g_button_bits[button]) ? 1 : 0;
}

/* ------------------------------------------------------------------------- */

const char* PCFX_Win32_InputButtonName(int button)
{
    if(button < 0 || button >= PCFX_WIN32_BUTTONS)
        return "?";
    return g_button_names[button];
}

void PCFX_Win32_InputSetWindow(HWND hwnd)
{
    g_hwnd = hwnd;
    PCFX_Win32_InputResetMouse();
}

void PCFX_Win32_InputResetMouse(void)
{
    g_mouse_valid = 0;
    g_mouse_dx = 0;
    g_mouse_dy = 0;
}

void PCFX_Win32_InputDefaults(void)
{
    memset(g_keymap, 0, sizeof(g_keymap));

    g_keymap[0][0] = VK_UP;
    g_keymap[0][1] = VK_DOWN;
    g_keymap[0][2] = VK_LEFT;
    g_keymap[0][3] = VK_RIGHT;
    g_keymap[0][4] = 'Z';
    g_keymap[0][5] = 'X';
    g_keymap[0][6] = 'C';
    g_keymap[0][7] = 'A';
    g_keymap[0][8] = 'S';
    g_keymap[0][9] = 'D';
    g_keymap[0][10] = VK_RETURN;
    g_keymap[0][11] = VK_RSHIFT;
    g_keymap[0][12] = 'Z';
    g_keymap[0][13] = 'X';

    g_keymap[1][0] = 'I';
    g_keymap[1][1] = 'K';
    g_keymap[1][2] = 'J';
    g_keymap[1][3] = 'L';
    g_keymap[1][4] = VK_NUMPAD1;
    g_keymap[1][5] = VK_NUMPAD2;
    g_keymap[1][6] = VK_NUMPAD3;
    g_keymap[1][7] = VK_NUMPAD4;
    g_keymap[1][8] = VK_NUMPAD5;
    g_keymap[1][9] = VK_NUMPAD6;
    g_keymap[1][10] = VK_NUMPAD0;
    g_keymap[1][11] = VK_DECIMAL;
    g_keymap[1][12] = VK_NUMPAD1;
    g_keymap[1][13] = VK_NUMPAD2;

    for(int i = 0; i < PCFX_WIN32_BUTTONS && i < 19; i++)
        option.config_buttons[i] = g_keymap[0][i];
}

void PCFX_Win32_InputSetMapping(int player, int button, uint32_t vk)
{
    if(player < 0 || player >= PCFX_WIN32_PLAYERS || button < 0 || button >= PCFX_WIN32_BUTTONS)
        return;
    g_keymap[player][button] = vk;
    if(player == 0 && button < 19)
        option.config_buttons[button] = vk;
}

uint32_t PCFX_Win32_InputGetMapping(int player, int button)
{
    if(player < 0 || player >= PCFX_WIN32_PLAYERS || button < 0 || button >= PCFX_WIN32_BUTTONS)
        return 0;
    return g_keymap[player][button];
}

void PCFX_Win32_InputVKName(uint32_t vk, char* out, unsigned out_size)
{
    if(!out || !out_size)
        return;
    if(!vk)
    {
        snprintf(out, out_size, "Unmapped");
        return;
    }

    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    LONG lparam = (LONG)(scan << 16);
    switch(vk)
    {
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_INSERT: case VK_DELETE:
        case VK_DIVIDE: case VK_NUMLOCK:
            lparam |= 0x01000000;
            break;
        default:
            break;
    }
    if(GetKeyNameTextA(lparam, out, (int)out_size) <= 0)
        snprintf(out, out_size, "VK 0x%02lX", (unsigned long)vk);
}

void PCFX_Win32_InputSetGamepadEnabled(int enabled)
{
    g_gamepad_enabled = enabled ? 1 : 0;
}

int PCFX_Win32_InputGetGamepadEnabled(void)
{
    return g_gamepad_enabled;
}

static int key_down(uint32_t vk)
{
    if(!vk)
        return 0;
    return (GetAsyncKeyState((int)vk) & 0x8000) ? 1 : 0;
}

int PCFX_Win32_InputButtonDown(int player, int button)
{
    if(player < 0 || player >= PCFX_WIN32_PLAYERS || button < 0 || button >= PCFX_WIN32_BUTTONS)
        return 0;
    gamepad_poll();
    return key_down(g_keymap[player][button]) || gamepad_button_down((unsigned)player, button);
}

void Read_General_Input(void)
{
    if(key_down(VK_F12))
        exit_vb = 1;

    gamepad_poll();

    if(!g_hwnd)
        return;

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_hwnd, &pt);
    if(g_mouse_valid)
    {
        g_mouse_dx = pt.x - g_prev_mouse.x;
        g_mouse_dy = pt.y - g_prev_mouse.y;
    }
    else
    {
        g_mouse_dx = 0;
        g_mouse_dy = 0;
        g_mouse_valid = 1;
    }
    g_prev_mouse = pt;
}

uint16_t Read_Pad_Input_Player(unsigned player)
{
    if(player >= PCFX_WIN32_PLAYERS)
        return 0;

    uint16_t button = 0;
    for(int i = 0; i < 12; i++)
    {
        if(key_down(g_keymap[player][i]))
            button |= g_button_bits[i];
    }
    button |= gamepad_pad_mask(player);
    return button;
}

uint16_t Read_Pad_Input(void)
{
    return Read_Pad_Input_Player(0);
}

int32_t Read_Mouse_X(void)
{
    return g_mouse_dx;
}

int32_t Read_Mouse_Y(void)
{
    return g_mouse_dy;
}

uint16_t Read_Mouse_buttons(void)
{
    uint16_t button = 0;
    if(key_down(g_keymap[0][12]) || (GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        button |= 1;
    if(key_down(g_keymap[0][13]) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
        button |= 2;
    return button;
}
