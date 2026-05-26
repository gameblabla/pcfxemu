#include <stdint.h>
#include <string.h>
#include "video_blit.h"

static uint16_t headless_framebuffer[512 * 240];
uint16_t* __restrict__ internal_pix = headless_framebuffer;
const uint32_t internal_pitch = 512;
static int active_x = 0;
static int active_y = 0;
static int active_w = 256;
static int active_h = 240;
static int active_fxga = 0;

void Clear_Video(void)
{
    memset(headless_framebuffer, 0, sizeof(headless_framebuffer));
}

void Init_Video(void)
{
    active_x = 0;
    active_y = 0;
    active_w = 256;
    active_h = 240;
    active_fxga = 0;
    internal_pix = headless_framebuffer + active_x;
    Clear_Video();
}

void Set_Video_Menu(void) { Clear_Video(); }
void Set_Video_InGame(void) { internal_pix = headless_framebuffer + active_x; }

extern "C" void pcfx_headless_video_set_full_width(int fxga_active)
{
    fxga_active = fxga_active ? 1 : 0;
    if(active_fxga != fxga_active)
        Clear_Video();

    active_fxga = fxga_active;
    // The backing buffer is wide enough for PC-Engine-style VDC modes used by
    // the PC-FX/PC-FXGA libraries.  The visible rectangle remains 256 pixels
    // until the core reports a wider active line.
    active_x = 0;
    active_y = 0;
    active_w = 256;
    active_h = 240;
    internal_pix = headless_framebuffer + active_x;
}

extern "C" void pcfx_headless_video_set_display_width(int width)
{
    if(width < 1) width = 256;
    if(width > 512) width = 512;
    active_w = width;
    if(active_w > 256)
    {
        active_x = 0;
        internal_pix = headless_framebuffer;
    }
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
    if(width) *width = 512;
    if(height) *height = 240;
    if(pitch_pixels) *pitch_pixels = (int)internal_pitch;
    return headless_framebuffer;
}
