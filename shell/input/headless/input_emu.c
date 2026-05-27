#include <stdint.h>
#include "input_emu.h"

static uint16_t headless_pad_state[2];
static int32_t headless_mouse_x;
static int32_t headless_mouse_y;
static uint16_t headless_mouse_buttons;

void pcfx_headless_input_set_pad(uint16_t state)
{
    headless_pad_state[0] = state;
}

void pcfx_headless_input_set_pad_player(unsigned player, uint16_t state)
{
    if(player < 2)
        headless_pad_state[player] = state;
}

uint16_t pcfx_headless_input_get_pad(void)
{
    return headless_pad_state[0];
}

uint16_t pcfx_headless_input_get_pad_player(unsigned player)
{
    return player < 2 ? headless_pad_state[player] : 0;
}

void pcfx_headless_input_set_mouse(int32_t dx, int32_t dy, uint16_t buttons)
{
    headless_mouse_x = dx;
    headless_mouse_y = dy;
    headless_mouse_buttons = buttons;
}

void Read_General_Input(void)
{
}

uint16_t Read_Pad_Input(void)
{
    return headless_pad_state[0];
}

uint16_t Read_Pad_Input_Player(unsigned player)
{
    return player < 2 ? headless_pad_state[player] : 0;
}

int32_t Read_Mouse_X(void)
{
    int32_t ret = headless_mouse_x;
    headless_mouse_x = 0;
    return ret;
}

int32_t Read_Mouse_Y(void)
{
    int32_t ret = headless_mouse_y;
    headless_mouse_y = 0;
    return ret;
}

uint16_t Read_Mouse_buttons(void)
{
    return headless_mouse_buttons;
}

