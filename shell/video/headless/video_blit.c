#include <stdint.h>
#include <string.h>
#include "video_blit.h"
#include "pcfx_dotwidth.h"

static MDFN_Pixel headless_framebuffer[1024 * 480];
static MDFN_Pixel output_framebuffer[1024 * 480];
/* Post-mixer, pre-presentation analog bandwidth approximation. Core's raw
   framebuffer is NEVER modified. All filtering stays within each scanline. */
static MDFN_Pixel analog_framebuffer[1024 * 480];
static int presentation_dirty = 1;
static int analog_mode = 0;  /* 0=off, 1=component approximation, 2=S-Video approximation */
void pcfx_headless_video_set_analog_mode(int mode)
{
    if(mode < 0 || mode > 2) mode = 0;
    if(mode != analog_mode) presentation_dirty = 1;
    analog_mode = mode;
}
int pcfx_headless_video_get_analog_mode(void) { return analog_mode; }

static int width_mode = PCFX_WIDTH_AUTO;
static int presented_width = 256;
MDFN_Pixel* __restrict__ internal_pix = headless_framebuffer;
const uint32_t internal_pitch = 1024;
static int active_x = 0;
static int active_y = 0;
static int active_w = 256;
static int active_h = 240;
static int active_fxga = 0;
static int active_interlaced = 0;
static int last_mixed_odd = 0;
#if !defined(MDFN_PIXEL_FORMAT_RGB565)
static uint16_t rgb565_shadow[1024 * 480];
#endif

void Clear_Video(void)
{
    memset(headless_framebuffer, 0, sizeof(headless_framebuffer));
    presentation_dirty = 1;
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
    active_interlaced = 0;
    last_mixed_odd = 0;
    internal_pix = headless_framebuffer + active_x;
    Clear_Video();
}

void Set_Video_Menu(void) { Clear_Video(); }
void Set_Video_InGame(void) { internal_pix = headless_framebuffer + active_x; }

/* The mode is selected by TETSU, never by the screenshot caller.
   Keep the previous field intact until its partner completes. */
void pcfx_headless_video_set_interlaced(int interlaced)
{
    interlaced = !!interlaced;
    if(active_interlaced != interlaced)
    {
        active_interlaced = interlaced;
        Clear_Video();
    }
    active_h = interlaced ? 480 : 240;
}

void pcfx_headless_video_note_field(int odd)
{
    last_mixed_odd = !!odd;
}

int pcfx_headless_video_last_field_is_odd(void) { return last_mixed_odd; }
int pcfx_headless_video_is_interlaced(void) { return active_interlaced; }
/* A frame buffer cannot encode NTSC equalisation/serration sync pulses.
   This exposes the documented field half-line offset to an analog waveform
   exporter without pretending that the PNG or SDL texture contains sync. */
int pcfx_headless_video_field_half_line_offset(void)
{
    return active_interlaced && !last_mixed_odd ? 1 : 0;
}

void pcfx_headless_video_set_full_width(int fxga_active)
{
    fxga_active = fxga_active ? 1 : 0;
    if(active_fxga != fxga_active)
        Clear_Video();

    active_fxga = fxga_active;
    active_x = 0;
    active_y = 0;
    active_w = 256;
    active_h = active_interlaced ? 480 : 240;
    internal_pix = headless_framebuffer + active_x;
}

void pcfx_headless_video_set_display_width(int width)
{
    if(width < 1) width = 256;
    if(width > 1024) width = 1024;
    active_w = width;
    presentation_dirty = 1;
    if(active_w > 256)
    {
        active_x = 0;
        internal_pix = headless_framebuffer;
    }
}


void pcfx_headless_video_set_width_mode(int mode)
{
    if(mode < PCFX_WIDTH_AUTO || mode > PCFX_WIDTH_CLOCK)
        mode = PCFX_WIDTH_AUTO;
    if(width_mode != mode) presentation_dirty = 1;
    width_mode = mode;
}

int pcfx_headless_video_get_width_mode(void) { return width_mode; }

/* These are illustrative, zero-phase spatial bandwidth presets, not a
   measured transfer function for the PC-FX DAC or external encoder.
   They preserve the full 21MHz clock grid until the output resampling step.
   Channel filtering uses one luma channel and two colour differences,
   unlike a naive RGB blur which would overblur luminance edges. */
static void analog_unpack(MDFN_Pixel p, int* r, int* g, int* b)
{
#if defined(MDFN_PIXEL_FORMAT_RGB565)
    *r = (((p >> 11)&31)*255+15)/31;
    *g = (((p >> 5)&63)*255+31)/63;
    *b = ((p&31)*255+15)/31;
#elif defined(MDFN_PIXEL_FORMAT_RGB555)
    *r = (((p >> 10)&31)*255+15)/31;
    *g = (((p >> 5)&31)*255+15)/31;
    *b = ((p&31)*255+15)/31;
#else
    *r = (p >> RED_SHIFT)&255;
    *g = (p >> GREEN_SHIFT)&255;
    *b = (p >> BLUE_SHIFT)&255;
#endif
}
static int analog_clip(int v) { return v<0 ? 0 : (v>255 ? 255 : v); }
static MDFN_Pixel analog_pack(int r, int g, int b)
{
    r=analog_clip(r); g=analog_clip(g); b=analog_clip(b);
#if defined(MDFN_PIXEL_FORMAT_RGB565)
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);
#elif defined(MDFN_PIXEL_FORMAT_RGB555)
    return ((r>>3)<<10)|((g>>3)<<5)|(b>>3);
#else
    return MAKECOLOR(r,g,b,255);
#endif
}
static void analog_filter_rows(const MDFN_Pixel* src, MDFN_Pixel* dest,
                               int w, int h, int pitch, int mode)
{
    /* Normalised symmetric integer kernels; zero phase to avoid shifting
       measured TETSU edge positions. Component has wider bandwidth. */
    static const int y_comp[] = {1,14,1};
    static const int c_comp[] = {1,6,1};
    static const int y_svid[] = {1,6,1};
    static const int c_svid[] = {1,6,15,20,15,6,1};
    const int* yw = mode == 1 ? y_comp : y_svid;
    const int* cw = mode == 1 ? c_comp : c_svid;
    const int yn = 3, cn = mode == 1 ? 3 : 7;
    const int yd = mode == 1 ? 16 : 8, cd = mode == 1 ? 8 : 64;
    for(int row=0;row<h;row++)
    {
        const MDFN_Pixel* line = src + row*pitch;
        MDFN_Pixel* out = dest+row*pitch;
        for(int x=0;x<w;x++)
        {
            int sy=0, su=0, sv=0;
            for(int k=0;k<cn;k++)
            {
                int sx=x+k-cn/2;
                int r,g,b;
                if(sx<0) sx=0;
                if(sx>=w) sx=w-1;
                analog_unpack(line[sx],&r,&g,&b);
                const int y=(77*r+150*g+29*b+128)>>8;
                su+=cw[k]*(b-y);
                sv+=cw[k]*(r-y);
            }
            for(int k=0;k<yn;k++)
            {
                int sx=x+k-yn/2;
                int r,g,b;
                if(sx<0) sx=0;
                if(sx>=w) sx=w-1;
                analog_unpack(line[sx],&r,&g,&b);
                sy+=yw[k]*((77*r+150*g+29*b+128)>>8);
            }
            const int y=(sy+yd/2)/yd;
            const int b=y+(su>=0 ? (su+cd/2)/cd : (su-cd/2)/cd);
            const int r=y+(sv>=0 ? (sv+cd/2)/cd : (sv-cd/2)/cd);
            const int g=(256*y-77*r-29*b+75)/150;
            out[x]=analog_pack(r,g,b);
        }
    }
}

static void prepare_presentation(void)
{
    if(!presentation_dirty) return;
    presented_width = pcfx_dotwidth_choose(width_mode, active_w,
                         headless_framebuffer, (int)internal_pitch, active_h,
                         (int)sizeof(MDFN_Pixel));
    if(analog_mode)
        analog_filter_rows(headless_framebuffer, analog_framebuffer, active_w,
                           active_h, (int)internal_pitch, analog_mode);
    const MDFN_Pixel *source = analog_mode ? analog_framebuffer : headless_framebuffer;
    if(presented_width != active_w)
        pcfx_dotwidth_downsample(output_framebuffer, source,
                            (int)internal_pitch, active_h, presented_width,
                            (int)sizeof(MDFN_Pixel));
    presentation_dirty = 0;
}

/* Raw lossless mixer output is also available independently of the chosen
 * window or screenshot width, for video analysis / higher-quality shaders. */
const MDFN_Pixel* pcfx_headless_video_raw_pixels(int* w, int* h, int* pitch)
{
    if(w) *w = 1024;
    if(h) *h = 480;
    if(pitch) *pitch = (int)internal_pitch;
    return headless_framebuffer;
}

void pcfx_headless_video_get_display_rect(int* x, int* y, int* w, int* h)
{
    if(x) *x = active_x;
    if(y) *y = active_y;
    prepare_presentation();
    if(w) *w = presented_width;
    if(h) *h = active_h;
}
void Video_Close(void) {}
void Update_Video_Menu(void) {}
void Update_Video_Ingame(void) {}

const MDFN_Pixel* pcfx_headless_video_pixels(int* width, int* height, int* pitch_pixels)
{
    if(width) *width = 1024;
    if(height) *height = 480;
    if(pitch_pixels) *pitch_pixels = (int)internal_pitch;
    prepare_presentation();
    return presented_width == active_w ? (analog_mode ? analog_framebuffer : headless_framebuffer) : output_framebuffer;
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

static uint8_t rgba8_shadow[1024 * 480 * 4];

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
