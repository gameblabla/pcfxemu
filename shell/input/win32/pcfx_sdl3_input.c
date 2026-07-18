/* SDL3 gamepad input backend for the Win64 PCFXEmu frontend.
 * See pcfx_sdl3_input.h. Compiled only when PCFX_WIN32_HAVE_SDL3 is defined. */

#include "pcfx_sdl3_input.h"
#include "input_win32.h"   /* PCFX_WIN32_PLAYERS */

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   /* SDL_SetMainReady() */

#include <stdio.h>
#include <string.h>

/* PC-FX pad button bits, matching g_button_bits[] in input_emu.c. */
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

#define AXIS_DEADZONE 16000

static int          g_ready;                       /* SDL gamepad subsystem up */
static int          g_quit;                        /* SDL_EVENT_QUIT seen */
static SDL_Gamepad *g_pads[PCFX_WIN32_PLAYERS];    /* one pad per emulated player */
static char         g_status[128] = "SDL3 gamepad input disabled";

/* Assign a freshly connected gamepad to the first empty player slot. */
static void assign_gamepad(SDL_JoystickID id)
{
    if(!SDL_IsGamepad(id))
        return;
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        if(g_pads[p] && SDL_GetGamepadID(g_pads[p]) == id)
            return; /* already open */
    }
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        if(!g_pads[p])
        {
            g_pads[p] = SDL_OpenGamepad(id);
            return;
        }
    }
}

static void remove_gamepad(SDL_JoystickID id)
{
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        if(g_pads[p] && SDL_GetGamepadID(g_pads[p]) == id)
        {
            SDL_CloseGamepad(g_pads[p]);
            g_pads[p] = NULL;
        }
    }
}

int PCFX_Win32_SDL3_Init(void)
{
    if(g_ready)
        return 1;

    SDL_SetMainReady();
    /* SDL_INIT_GAMEPAD implies SDL_INIT_JOYSTICK and pulls in the event queue. */
    if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
    {
        const char *err = SDL_GetError();
        snprintf(g_status, sizeof(g_status), "SDL3 gamepad init failed: %s",
                 (err && err[0]) ? err : "unknown");
        return 0;
    }
    g_ready = 1;

    /* Open pads that are already connected at startup. */
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if(ids)
    {
        for(int i = 0; i < count; i++)
            assign_gamepad(ids[i]);
        SDL_free(ids);
    }

    int opened = 0;
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
        if(g_pads[p])
            opened++;
    snprintf(g_status, sizeof(g_status), "SDL3 gamepad input: %d pad%s connected",
             opened, opened == 1 ? "" : "s");
    return 1;
}

void PCFX_Win32_SDL3_Shutdown(void)
{
    if(!g_ready)
        return;
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        if(g_pads[p])
        {
            SDL_CloseGamepad(g_pads[p]);
            g_pads[p] = NULL;
        }
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    g_ready = 0;
    strcpy(g_status, "SDL3 gamepad input disabled");
}

void PCFX_Win32_SDL3_Poll(void)
{
    if(!g_ready)
        return;

    SDL_Event ev;
    while(SDL_PollEvent(&ev))
    {
        switch(ev.type)
        {
            case SDL_EVENT_QUIT:
                g_quit = 1;
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                assign_gamepad(ev.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                remove_gamepad(ev.gdevice.which);
                break;
            default:
                break;
        }
    }
    SDL_UpdateGamepads();
}

uint16_t PCFX_Win32_SDL3_PadButtons(unsigned player)
{
    if(!g_ready || player >= PCFX_WIN32_PLAYERS)
        return 0;

    SDL_Gamepad *pad = g_pads[player];
    if(!pad)
        return 0;

    uint16_t out = 0;

    /* Face buttons -> PC-FX I..VI + shoulders. */
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH))          out |= PADB_I;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST))           out |= PADB_II;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST))           out |= PADB_III;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH))          out |= PADB_IV;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  out |= PADB_V;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) out |= PADB_VI;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))          out |= PADB_RUN;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK))           out |= PADB_SELECT;

    /* D-pad and left stick both drive the direction pad. */
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP))    out |= PADB_UP;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  out |= PADB_DOWN;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  out |= PADB_LEFT;
    if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) out |= PADB_RIGHT;

    Sint16 lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    Sint16 ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    if(lx < -AXIS_DEADZONE) out |= PADB_LEFT;
    if(lx >  AXIS_DEADZONE) out |= PADB_RIGHT;
    if(ly < -AXIS_DEADZONE) out |= PADB_UP;
    if(ly >  AXIS_DEADZONE) out |= PADB_DOWN;

    return out;
}

int PCFX_Win32_SDL3_QuitRequested(void)
{
    return g_quit;
}

const char *PCFX_Win32_SDL3_Status(void)
{
    return g_status;
}
