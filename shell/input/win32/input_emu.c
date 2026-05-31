#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "input_emu.h"
#include "input_win32.h"
#include "config.h"

extern uint8_t exit_vb;
extern uint32_t emulator_state;

#define XI_NONE       0u
#define XI_LT         0x00010000u
#define XI_RT         0x00010001u
#define XI_LS_LEFT    0x00010002u
#define XI_LS_RIGHT   0x00010003u
#define XI_LS_UP      0x00010004u
#define XI_LS_DOWN    0x00010005u
#define XI_RS_LEFT    0x00010006u
#define XI_RS_RIGHT   0x00010007u
#define XI_RS_UP      0x00010008u
#define XI_RS_DOWN    0x00010009u

#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

static HWND g_hwnd;
static POINT g_prev_mouse;
static int g_mouse_valid;
static int32_t g_mouse_dx;
static int32_t g_mouse_dy;

static uint32_t g_keymap[PCFX_WIN32_PLAYERS][PCFX_WIN32_BUTTONS];
static uint32_t g_xmap[PCFX_WIN32_PLAYERS][PCFX_WIN32_BUTTONS];
static int g_xinput_enabled = 1;
static int g_xinput_user[PCFX_WIN32_PLAYERS] = { 0, 1 };
static XINPUT_STATE g_xstate[PCFX_WIN32_XINPUT_USERS];
static int g_xconnected[PCFX_WIN32_XINPUT_USERS];
static int g_xpoll_valid;

static const char* const g_button_names[PCFX_WIN32_BUTTONS] = {
    "Up", "Down", "Left", "Right",
    "I / A", "II / B", "III / C", "IV / X", "V / Y", "VI / Z",
    "Run / Start", "Select", "Mouse Left", "Mouse Right"
};

static const uint16_t g_button_bits[12] = {
    256, 1024, 2048, 512,
    1, 2, 4, 8, 16, 32,
    128, 64
};

typedef struct XInputName
{
    uint32_t code;
    const char* name;
} XInputName;

static const XInputName g_xinput_names[] = {
    { XINPUT_GAMEPAD_DPAD_UP,        "D-Pad Up" },
    { XINPUT_GAMEPAD_DPAD_DOWN,      "D-Pad Down" },
    { XINPUT_GAMEPAD_DPAD_LEFT,      "D-Pad Left" },
    { XINPUT_GAMEPAD_DPAD_RIGHT,     "D-Pad Right" },
    { XINPUT_GAMEPAD_START,          "Start" },
    { XINPUT_GAMEPAD_BACK,           "Back" },
    { XINPUT_GAMEPAD_LEFT_THUMB,     "Left Stick Click" },
    { XINPUT_GAMEPAD_RIGHT_THUMB,    "Right Stick Click" },
    { XINPUT_GAMEPAD_LEFT_SHOULDER,  "Left Shoulder" },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, "Right Shoulder" },
    { XINPUT_GAMEPAD_A,              "A" },
    { XINPUT_GAMEPAD_B,              "B" },
    { XINPUT_GAMEPAD_X,              "X" },
    { XINPUT_GAMEPAD_Y,              "Y" },
    { XI_LT,                         "Left Trigger" },
    { XI_RT,                         "Right Trigger" },
    { XI_LS_LEFT,                    "Left Stick Left" },
    { XI_LS_RIGHT,                   "Left Stick Right" },
    { XI_LS_UP,                      "Left Stick Up" },
    { XI_LS_DOWN,                    "Left Stick Down" },
    { XI_RS_LEFT,                    "Right Stick Left" },
    { XI_RS_RIGHT,                   "Right Stick Right" },
    { XI_RS_UP,                      "Right Stick Up" },
    { XI_RS_DOWN,                    "Right Stick Down" }
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#if defined(PCFX_WIN32_XP_COMPAT)
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
static HMODULE g_xinput_dll;
static PFN_XInputGetState g_XInputGetState;
static int g_xinput_load_tried;

static void xinput_load_runtime(void)
{
    static const char* const dlls[] = {
        "xinput1_3.dll",      /* DirectX runtime, available on XP when installed. */
        "xinput9_1_0.dll",    /* Vista/7 system component. */
        "xinput1_4.dll",      /* Windows 8+. */
        "xinput1_2.dll",
        "xinput1_1.dll"
    };

    if(g_xinput_load_tried)
        return;
    g_xinput_load_tried = 1;
    for(size_t i = 0; i < ARRAY_SIZE(dlls); i++)
    {
        g_xinput_dll = LoadLibraryA(dlls[i]);
        if(g_xinput_dll)
        {
            g_XInputGetState = (PFN_XInputGetState)GetProcAddress(g_xinput_dll, "XInputGetState");
            if(g_XInputGetState)
                return;
            FreeLibrary(g_xinput_dll);
            g_xinput_dll = NULL;
        }
    }
}

static DWORD pcfx_xinput_get_state(DWORD user, XINPUT_STATE* state)
{
    xinput_load_runtime();
    if(!g_XInputGetState)
        return ERROR_DEVICE_NOT_CONNECTED;
    return g_XInputGetState(user, state);
}
#else
#define pcfx_xinput_get_state XInputGetState
#endif

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
    memset(g_xmap, 0, sizeof(g_xmap));

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

    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        g_xinput_user[p] = p;
        g_xmap[p][0] = XINPUT_GAMEPAD_DPAD_UP;
        g_xmap[p][1] = XINPUT_GAMEPAD_DPAD_DOWN;
        g_xmap[p][2] = XINPUT_GAMEPAD_DPAD_LEFT;
        g_xmap[p][3] = XINPUT_GAMEPAD_DPAD_RIGHT;
        g_xmap[p][4] = XINPUT_GAMEPAD_A;
        g_xmap[p][5] = XINPUT_GAMEPAD_B;
        g_xmap[p][6] = XINPUT_GAMEPAD_X;
        g_xmap[p][7] = XINPUT_GAMEPAD_Y;
        g_xmap[p][8] = XINPUT_GAMEPAD_LEFT_SHOULDER;
        g_xmap[p][9] = XINPUT_GAMEPAD_RIGHT_SHOULDER;
        g_xmap[p][10] = XINPUT_GAMEPAD_START;
        g_xmap[p][11] = XINPUT_GAMEPAD_BACK;
        g_xmap[p][12] = XINPUT_GAMEPAD_A;
        g_xmap[p][13] = XINPUT_GAMEPAD_B;
    }

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

void PCFX_Win32_InputSetXInputEnabled(int enabled)
{
    g_xinput_enabled = enabled ? 1 : 0;
    memset(g_xconnected, 0, sizeof(g_xconnected));
    g_xpoll_valid = 0;
}

int PCFX_Win32_InputGetXInputEnabled(void)
{
    return g_xinput_enabled;
}

void PCFX_Win32_InputSetXInputUser(int player, int user_index)
{
    if(player < 0 || player >= PCFX_WIN32_PLAYERS)
        return;
    if(user_index < 0)
        user_index = 0;
    if(user_index >= PCFX_WIN32_XINPUT_USERS)
        user_index = PCFX_WIN32_XINPUT_USERS - 1;
    g_xinput_user[player] = user_index;
}

int PCFX_Win32_InputGetXInputUser(int player)
{
    if(player < 0 || player >= PCFX_WIN32_PLAYERS)
        return 0;
    return g_xinput_user[player];
}

void PCFX_Win32_InputSetXInputMapping(int player, int button, uint32_t code)
{
    if(player < 0 || player >= PCFX_WIN32_PLAYERS || button < 0 || button >= PCFX_WIN32_BUTTONS)
        return;
    g_xmap[player][button] = code;
}

uint32_t PCFX_Win32_InputGetXInputMapping(int player, int button)
{
    if(player < 0 || player >= PCFX_WIN32_PLAYERS || button < 0 || button >= PCFX_WIN32_BUTTONS)
        return 0;
    return g_xmap[player][button];
}

void PCFX_Win32_InputXInputName(uint32_t code, char* out, unsigned out_size)
{
    if(!out || !out_size)
        return;
    if(!code)
    {
        snprintf(out, out_size, "Unmapped");
        return;
    }
    for(size_t i = 0; i < ARRAY_SIZE(g_xinput_names); i++)
    {
        if(g_xinput_names[i].code == code)
        {
            snprintf(out, out_size, "%s", g_xinput_names[i].name);
            return;
        }
    }
    snprintf(out, out_size, "XInput 0x%08lX", (unsigned long)code);
}

static int key_down(uint32_t vk)
{
    if(!vk)
        return 0;
    return (GetAsyncKeyState((int)vk) & 0x8000) ? 1 : 0;
}

static void xinput_poll(void)
{
    if(!g_xinput_enabled)
    {
        memset(g_xconnected, 0, sizeof(g_xconnected));
        g_xpoll_valid = 1;
        return;
    }

    for(DWORD i = 0; i < PCFX_WIN32_XINPUT_USERS; i++)
    {
        memset(&g_xstate[i], 0, sizeof(g_xstate[i]));
        g_xconnected[i] = (pcfx_xinput_get_state(i, &g_xstate[i]) == ERROR_SUCCESS) ? 1 : 0;
    }
    g_xpoll_valid = 1;
}

static int xinput_code_down(const XINPUT_GAMEPAD* gp, uint32_t code)
{
    if(!gp || !code)
        return 0;

    switch(code)
    {
        case XI_LT:       return gp->bLeftTrigger  > 64;
        case XI_RT:       return gp->bRightTrigger > 64;
        case XI_LS_LEFT:  return gp->sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        case XI_LS_RIGHT: return gp->sThumbLX >  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        case XI_LS_UP:    return gp->sThumbLY >  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        case XI_LS_DOWN:  return gp->sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        case XI_RS_LEFT:  return gp->sThumbRX < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
        case XI_RS_RIGHT: return gp->sThumbRX >  XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
        case XI_RS_UP:    return gp->sThumbRY >  XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
        case XI_RS_DOWN:  return gp->sThumbRY < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
        default:
            if(code <= 0xFFFFu)
                return (gp->wButtons & (WORD)code) ? 1 : 0;
            return 0;
    }
}

static int xinput_player_down(unsigned player, uint32_t code)
{
    if(!g_xinput_enabled || player >= PCFX_WIN32_PLAYERS || !code)
        return 0;
    if(!g_xpoll_valid)
        xinput_poll();
    int user = g_xinput_user[player];
    if(user < 0 || user >= PCFX_WIN32_XINPUT_USERS || !g_xconnected[user])
        return 0;
    return xinput_code_down(&g_xstate[user].Gamepad, code);
}


int PCFX_Win32_InputButtonDown(int player, int button)
{
    if(player < 0 || player >= PCFX_WIN32_PLAYERS || button < 0 || button >= PCFX_WIN32_BUTTONS)
        return 0;
    xinput_poll();
    return key_down(g_keymap[player][button]) || xinput_player_down((unsigned)player, g_xmap[player][button]);
}

int PCFX_Win32_InputXInputCodeDown(int player, uint32_t code)
{
    if(player < 0 || player >= PCFX_WIN32_PLAYERS)
        return 0;
    xinput_poll();
    return xinput_player_down((unsigned)player, code);
}

int PCFX_Win32_InputPollXInputCapture(int player, uint32_t* code_out)
{
    if(!code_out || player < 0 || player >= PCFX_WIN32_PLAYERS || !g_xinput_enabled)
        return 0;

    XINPUT_STATE st;
    memset(&st, 0, sizeof(st));
    int user = g_xinput_user[player];
    if(user < 0 || user >= PCFX_WIN32_XINPUT_USERS)
        return 0;
    if(pcfx_xinput_get_state((DWORD)user, &st) != ERROR_SUCCESS)
        return 0;

    static const uint32_t capture_order[] = {
        XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
        XI_LT, XI_RT,
        XI_LS_LEFT, XI_LS_RIGHT, XI_LS_UP, XI_LS_DOWN,
        XI_RS_LEFT, XI_RS_RIGHT, XI_RS_UP, XI_RS_DOWN
    };

    for(size_t i = 0; i < ARRAY_SIZE(capture_order); i++)
    {
        if(xinput_code_down(&st.Gamepad, capture_order[i]))
        {
            *code_out = capture_order[i];
            return 1;
        }
    }
    return 0;
}

void Read_General_Input(void)
{
    if(key_down(VK_F12))
        exit_vb = 1;

    xinput_poll();

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
        if(key_down(g_keymap[player][i]) || xinput_player_down(player, g_xmap[player][i]))
            button |= g_button_bits[i];
    }
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
    if(key_down(g_keymap[0][12]) || xinput_player_down(0, g_xmap[0][12]) || (GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        button |= 1;
    if(key_down(g_keymap[0][13]) || xinput_player_down(0, g_xmap[0][13]) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
        button |= 2;
    return button;
}
