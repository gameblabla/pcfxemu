#include <stdint.h>
#include <string.h>
#include "video_blit.h"

static uint16_t headless_framebuffer[320 * 240];
uint16_t* __restrict__ internal_pix = headless_framebuffer + 32;
const uint32_t internal_pitch = 320;

void Clear_Video(void)
{
    memset(headless_framebuffer, 0, sizeof(headless_framebuffer));
}

void Init_Video(void)
{
    internal_pix = headless_framebuffer + 32;
    Clear_Video();
}

void Set_Video_Menu(void) { Clear_Video(); }
void Set_Video_InGame(void) { internal_pix = headless_framebuffer + 32; }
void Video_Close(void) {}
void Update_Video_Menu(void) {}
void Update_Video_Ingame(void) {}

extern "C" const uint16_t* pcfx_headless_video_rgb565(int* width, int* height, int* pitch_pixels)
{
    if(width) *width = 320;
    if(height) *height = 240;
    if(pitch_pixels) *pitch_pixels = (int)internal_pitch;
    return headless_framebuffer;
}
