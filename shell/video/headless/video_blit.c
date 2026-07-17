#include <stdint.h>
#include <string.h>
#include "video_blit.h"

static MDFN_Pixel headless_framebuffer[512 * 240];
MDFN_Pixel* __restrict__ internal_pix = headless_framebuffer;
const uint32_t internal_pitch = 512;
static int active_x = 0;
static int active_y = 0;
static int active_w = 256;
static int active_h = 240;
static int active_fxga = 0;
#if !defined(MDFN_PIXEL_FORMAT_RGB565)
static uint16_t rgb565_shadow[512 * 240];
#endif

void Clear_Video(void)
{
    memset(headless_framebuffer, 0, sizeof(headless_framebuffer));
#if !defined(MDFN_PIXEL_FORMAT_RGB565)
    memset(rgb565_shadow, 0, sizeof(rgb565_shadow));
#endif
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

void pcfx_headless_video_set_full_width(int fxga_active)
{
    fxga_active = fxga_active ? 1 : 0;
    if(active_fxga != fxga_active)
        Clear_Video();

    active_fxga = fxga_active;
    active_x = 0;
    active_y = 0;
    active_w = 256;
    active_h = 240;
    internal_pix = headless_framebuffer + active_x;
}

void pcfx_headless_video_set_display_width(int width)
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

void pcfx_headless_video_get_display_rect(int* x, int* y, int* w, int* h)
{
    if(x) *x = active_x;
    if(y) *y = active_y;
    if(w) *w = active_w;
    if(h) *h = active_h;
}
void Video_Close(void) {}
void Update_Video_Menu(void) {}
void Update_Video_Ingame(void) {}

const MDFN_Pixel* pcfx_headless_video_pixels(int* width, int* height, int* pitch_pixels)
{
    if(width) *width = 512;
    if(height) *height = 240;
    if(pitch_pixels) *pitch_pixels = (int)internal_pitch;
    return headless_framebuffer;
}

int pcfx_headless_video_bytes_per_pixel(void)
{
    return MDFN_VIDEO_BYTES_PER_PIXEL;
}

int pcfx_headless_video_pixel_format(void)
{
#if defined(MDFN_PIXEL_FORMAT_RGBA8888)
    return 2;
#elif defined(MDFN_PIXEL_FORMAT_ARGB8888)
    return 3;
#elif defined(MDFN_PIXEL_FORMAT_XRGB8888)
    return 4;
#elif defined(MDFN_PIXEL_FORMAT_RGB555)
    return 5;
#else
    return 1;
#endif
}

#if !defined(MDFN_PIXEL_FORMAT_RGB565)
static uint16_t pixel_to_rgb565(MDFN_Pixel p)
{
    const unsigned r = (unsigned)((p >> RED_SHIFT) & 0xFFu);
    const unsigned g = (unsigned)((p >> GREEN_SHIFT) & 0xFFu);
    const unsigned b = (unsigned)((p >> BLUE_SHIFT) & 0xFFu);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
#endif

const uint16_t* pcfx_headless_video_rgb565(int* width, int* height, int* pitch_pixels)
{
#if defined(MDFN_PIXEL_FORMAT_RGB565)
    return (const uint16_t*)pcfx_headless_video_pixels(width, height, pitch_pixels);
#else
    int w = 0, h = 0, p = 0;
    const MDFN_Pixel *src = pcfx_headless_video_pixels(&w, &h, &p);
    for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++)
            rgb565_shadow[y * p + x] = pixel_to_rgb565(src[y * p + x]);
    if(width) *width = w;
    if(height) *height = h;
    if(pitch_pixels) *pitch_pixels = p;
    return rgb565_shadow;
#endif
}

/* Expand one native framebuffer pixel to 8-bit R, G, B, whatever the compiled
 * pixel format is.  Keeping this in one place (next to the surface.h macros the
 * core packs colours with) is what makes the PNG/RGBA export correct for every
 * build format and every graphics mode -- there is no second, format-specific
 * unpack path to drift out of sync. */
static void pixel_to_rgb888(MDFN_Pixel p, uint8_t* r, uint8_t* g, uint8_t* b)
{
#if defined(MDFN_PIXEL_FORMAT_RGB565)
    const unsigned R = (p >> 11) & 0x1F, G = (p >> 5) & 0x3F, B = p & 0x1F;
    *r = (uint8_t)((R * 255u + 15u) / 31u);
    *g = (uint8_t)((G * 255u + 31u) / 63u);
    *b = (uint8_t)((B * 255u + 15u) / 31u);
#elif defined(MDFN_PIXEL_FORMAT_RGB555)
    const unsigned R = (p >> 10) & 0x1F, G = (p >> 5) & 0x1F, B = p & 0x1F;
    *r = (uint8_t)((R * 255u + 15u) / 31u);
    *g = (uint8_t)((G * 255u + 15u) / 31u);
    *b = (uint8_t)((B * 255u + 15u) / 31u);
#else /* 32bpp: RGBA8888 / ARGB8888 / XRGB8888 all expose RED/GREEN/BLUE_SHIFT */
    *r = (uint8_t)((p >> RED_SHIFT) & 0xFFu);
    *g = (uint8_t)((p >> GREEN_SHIFT) & 0xFFu);
    *b = (uint8_t)((p >> BLUE_SHIFT) & 0xFFu);
#endif
}

static uint8_t rgba8_shadow[512 * 240 * 4];

const uint8_t* pcfx_headless_video_rgba8888(int* width, int* height, int* pitch_pixels)
{
    int w = 0, h = 0, p = 0;
    const MDFN_Pixel *src = pcfx_headless_video_pixels(&w, &h, &p);
    for(int y = 0; y < h; y++)
    {
        for(int x = 0; x < w; x++)
        {
            uint8_t *d = &rgba8_shadow[(y * p + x) * 4];
            pixel_to_rgb888(src[y * p + x], &d[0], &d[1], &d[2]);
            d[3] = 0xFF;
        }
    }
    if(width) *width = w;
    if(height) *height = h;
    if(pitch_pixels) *pitch_pixels = p;
    return rgba8_shadow;
}
