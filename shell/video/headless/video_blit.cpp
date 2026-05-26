#include <stdint.h>
#include <string.h>
#include "video_blit.h"

static uint16_t headless_framebuffer[320 * 240];
uint16_t* __restrict__ internal_pix = headless_framebuffer + 32;
const uint32_t internal_pitch = 320;
static int active_x = 32;
static int active_y = 0;
static int active_w = 256;
static int active_h = 240;
static int active_full_width = 0;

void Clear_Video(void)
{
    memset(headless_framebuffer, 0, sizeof(headless_framebuffer));
}

void Init_Video(void)
{
    active_x = 32;
    active_y = 0;
    active_w = 256;
    active_h = 240;
    active_full_width = 0;
    internal_pix = headless_framebuffer + active_x;
    Clear_Video();
}

void Set_Video_Menu(void) { Clear_Video(); }
void Set_Video_InGame(void) { internal_pix = headless_framebuffer + active_x; }

extern "C" void pcfx_headless_video_set_full_width(int full_width)
{
    full_width = full_width ? 1 : 0;
    if(active_full_width != full_width)
        Clear_Video();

    active_full_width = full_width;
    // The backing buffer is always 320 pixels.  A frame starts as a 256-pixel
    // PC-FX image; PC-FXGA/Aurora lines promote the visible rectangle to 320
    // through pcfx_headless_video_note_full_width_line().  PC-FXGA boot/BIOS
    // frames are commonly 256 pixels anchored at x=0, while ordinary PC-FX
    // frames are centered at x=32 in the backing buffer.
    active_x = full_width ? 0 : 32;
    active_y = 0;
    active_w = 256;
    active_h = 240;
    internal_pix = headless_framebuffer + active_x;
}

extern "C" void pcfx_headless_video_note_full_width_line(void)
{
    active_x = 0;
    active_y = 0;
    active_w = 320;
    active_h = 240;
}

extern "C" void pcfx_headless_video_get_display_rect(int* x, int* y, int* w, int* h)
{
    if(x) *x = active_x;
    if(y) *y = active_y;
    if(w) *w = active_w;
    if(h) *h = active_h;
}
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
