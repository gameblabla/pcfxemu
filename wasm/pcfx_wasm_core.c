#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pcfx_headless.h"
#include "mednafen/pcfx/pcfx.h"

bool PCFX_StateSerialize(void* data, size_t size);
bool PCFX_StateUnserialize(const void* data, size_t size);
size_t PCFX_StateSerializeSize(void);
size_t PCFX_StateLastSerializeSize(void);
bool PCFX_StateSerializeMalloc(uint8_t **out_data, size_t *out_size);
void PCFX_SetPreferFXGABIOS(bool enabled);
void PCFX_SetSystemMode(int mode);
void PCFX_SetHuC6273Enabled(bool enabled);

#define STATUS_NO_BIOS 0u
#define STATUS_BIOS_READY 1u
#define STATUS_MEDIA_READY 2u
#define STATUS_RUNNING 3u
#define STATUS_PAUSED 4u
#define STATUS_ERROR 5u

#define BIOS_PC_FX 1u
#define BIOS_PC_FXGA 2u
#define MEDIA_NONE 0u
#define MEDIA_CUE 1u
#define MEDIA_HUEXE 2u
#define MEDIA_CHD 3u
#define MEDIA_ISO_BIN 4u
#define MEDIA_AUDIO_CD 5u
#define MEDIA_M3U 6u
#define MEDIA_TOC 7u

extern uint32_t pcfx_wasm_vfs_add_file(uint32_t path_ptr, uint32_t path_len, uint32_t data_ptr, uint32_t size);
extern void pcfx_wasm_vfs_clear(void);
extern void *malloc(size_t size);
extern void __pcfx_wasm_heap_reset(void);
extern uint32_t __pcfx_wasm_heap_used(void);

static PCFX_Headless *emu;
static uint32_t system_mode;
static uint32_t status_code = STATUS_NO_BIOS;
static uint32_t error_code;
static uint32_t media_kind;
static uint32_t frame_counter;
static uint32_t huc6273_enabled = 1;
static uint32_t browser_smooth;
static uint32_t controller_type;
static char media_path[260];
static uint8_t *save_buf;
static uint32_t save_size;
static uint32_t save_capacity;
static uint32_t fallback_fb[344 * 240];
#define AUDIO_RING_FRAMES 16384u
static int16_t audio_ring[AUDIO_RING_FRAMES * 2u];
static uint32_t audio_ring_frames;

static uint32_t normalize_system_mode(uint32_t mode) { return mode == 1u ? 1u : mode == 2u ? 2u : 0u; }

static uint32_t fnv1a(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for(uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

__attribute__((export_name("pcfx_wasm_version")))
uint32_t pcfx_wasm_version(void) { return 0x00030010u; }

static void wasm_audio_callback(void *userdata, const int16_t *samples, uint32_t frames)
{
    (void)userdata;
    if(!samples || !frames)
        return;
    if(frames > AUDIO_RING_FRAMES)
    {
        samples += (frames - AUDIO_RING_FRAMES) * 2u;
        frames = AUDIO_RING_FRAMES;
    }
    if(audio_ring_frames + frames > AUDIO_RING_FRAMES)
    {
        uint32_t drop = (audio_ring_frames + frames) - AUDIO_RING_FRAMES;
        if(drop >= audio_ring_frames)
            audio_ring_frames = 0;
        else
        {
            memmove(audio_ring, audio_ring + drop * 2u, (audio_ring_frames - drop) * 2u * sizeof(audio_ring[0]));
            audio_ring_frames -= drop;
        }
    }
    memcpy(audio_ring + audio_ring_frames * 2u, samples, frames * 2u * sizeof(audio_ring[0]));
    audio_ring_frames += frames;
}

__attribute__((export_name("pcfx_wasm_malloc")))
uint32_t pcfx_wasm_malloc(uint32_t size) { return (uint32_t)(uintptr_t)malloc(size ? size : 1); }

__attribute__((export_name("pcfx_wasm_heap_used")))
uint32_t pcfx_wasm_heap_used(void) { return __pcfx_wasm_heap_used(); }

__attribute__((export_name("pcfx_wasm_reset_heap")))
void pcfx_wasm_reset_heap(void)
{
    if(emu) { pcfx_headless_destroy(emu); emu = NULL; }
    pcfx_wasm_vfs_clear();
    __pcfx_wasm_heap_reset();
    save_buf = NULL;
    save_size = 0;
    save_capacity = 0;
    audio_ring_frames = 0;
    media_kind = MEDIA_NONE;
    media_path[0] = 0;
    frame_counter = 0;
    status_code = STATUS_NO_BIOS;
    error_code = 0;
}

static void set_error(uint32_t code)
{
    error_code = code;
    status_code = STATUS_ERROR;
}

__attribute__((export_name("pcfx_wasm_init")))
void pcfx_wasm_init(uint32_t mode)
{
    if(emu) { pcfx_headless_destroy(emu); emu = NULL; }
    pcfx_wasm_vfs_clear();
    __pcfx_wasm_heap_reset();
    system_mode = normalize_system_mode(mode);
    status_code = STATUS_NO_BIOS;
    error_code = 0;
    media_kind = MEDIA_NONE;
    media_path[0] = 0;
    frame_counter = 0;
    audio_ring_frames = 0;
    save_buf = NULL;
    save_size = 0;
    save_capacity = 0;
    huc6273_enabled = 1;
    controller_type = 0;
}

__attribute__((export_name("pcfx_wasm_set_system_mode")))
void pcfx_wasm_set_system_mode(uint32_t mode) { system_mode = normalize_system_mode(mode); }

__attribute__((export_name("pcfx_wasm_set_3d_enabled")))
void pcfx_wasm_set_3d_enabled(uint32_t enabled)
{
    huc6273_enabled = enabled ? 1u : 0u;
    if(emu) PCFX_SetHuC6273Enabled(huc6273_enabled != 0u);
}

__attribute__((export_name("pcfx_wasm_get_3d_enabled")))
uint32_t pcfx_wasm_get_3d_enabled(void) { return huc6273_enabled; }

__attribute__((export_name("pcfx_wasm_set_browser_smooth")))
void pcfx_wasm_set_browser_smooth(uint32_t enabled) { browser_smooth = enabled ? 1u : 0u; }

__attribute__((export_name("pcfx_wasm_set_controller_type")))
void pcfx_wasm_set_controller_type(uint32_t type)
{
    controller_type = (type == 1u) ? 1u : 0u;
    if(emu)
        pcfx_headless_set_controller_type(emu, (uint8_t)controller_type);
}

__attribute__((export_name("pcfx_wasm_get_controller_type")))
uint32_t pcfx_wasm_get_controller_type(void) { return controller_type; }

__attribute__((export_name("pcfx_wasm_load_bios")))
uint32_t pcfx_wasm_load_bios(uint32_t ptr, uint32_t size, uint32_t kind)
{
    if(!ptr || size != 1024u * 1024u || (kind != BIOS_PC_FX && kind != BIOS_PC_FXGA))
    {
        set_error(0xB1050001u);
        return 0;
    }
    const char *name = (kind == BIOS_PC_FXGA) ? "/bios/pcfxga.rom" : "/bios/pcfx.rom";
    if(!pcfx_wasm_vfs_add_file((uint32_t)(uintptr_t)name, (uint32_t)strlen(name), ptr, size))
    {
        set_error(0xB1050002u);
        return 0;
    }
    status_code = STATUS_BIOS_READY;
    return fnv1a((const uint8_t*)(uintptr_t)ptr, size) ? 1u : 1u;
}

__attribute__((export_name("pcfx_wasm_set_media_path")))
uint32_t pcfx_wasm_set_media_path(uint32_t path_ptr, uint32_t path_len, uint32_t kind)
{
    if(!path_ptr || path_len == 0 || path_len >= sizeof(media_path)) { set_error(0xCD000001u); return 0; }
    memcpy(media_path, (const void*)(uintptr_t)path_ptr, path_len);
    media_path[path_len] = 0;
    media_kind = kind;
    status_code = STATUS_MEDIA_READY;
    return 1;
}

__attribute__((export_name("pcfx_wasm_set_media")))
uint32_t pcfx_wasm_set_media(uint32_t ptr, uint32_t sample_size, uint32_t total_lo, uint32_t total_hi, uint32_t kind, uint32_t file_count)
{
    (void)ptr; (void)sample_size; (void)total_lo; (void)total_hi; (void)file_count;
    media_kind = kind;
    status_code = STATUS_MEDIA_READY;
    return 1;
}

__attribute__((export_name("pcfx_wasm_start")))
uint32_t pcfx_wasm_start(void)
{
    if(emu) { pcfx_headless_destroy(emu); emu = NULL; }
    PCFX_SetSystemMode((int)system_mode);
    PCFX_HeadlessConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.bios_dir = "/bios";
    cfg.save_dir = "/save";
    cfg.sound_rate = 44100;
    cfg.disable_3d_hardware = huc6273_enabled ? 0 : 1;
    cfg.prefer_fxga_bios = (int)system_mode;
    emu = pcfx_headless_create(&cfg);
    if(!emu) { set_error(0xE0000001u); return 0; }
    pcfx_headless_set_audio_callback(emu, wasm_audio_callback, NULL);
    pcfx_headless_set_controller_type(emu, (uint8_t)controller_type);
    audio_ring_frames = 0;
    if(media_path[0])
    {
        if(!pcfx_headless_load_cd(emu, media_path)) { set_error(0xE0000002u); return 0; }
    }
    else
    {
        if(!pcfx_headless_boot_bios(emu)) { set_error(0xE0000003u); return 0; }
    }
    status_code = STATUS_RUNNING;
    error_code = 0;
    frame_counter = 0;
    return 1;
}

__attribute__((export_name("pcfx_wasm_swap_disc")))
uint32_t pcfx_wasm_swap_disc(uint32_t path_ptr, uint32_t path_len, uint32_t kind)
{
    if(!emu || status_code != STATUS_RUNNING) { set_error(0xCD000003u); return 0; }
    if(!path_ptr || path_len == 0 || path_len >= sizeof(media_path)) { set_error(0xCD000001u); return 0; }
    char new_path[sizeof(media_path)];
    memcpy(new_path, (const void*)(uintptr_t)path_ptr, path_len);
    new_path[path_len] = 0;
    if(!pcfx_headless_swap_disc(emu, new_path)) { set_error(0xE0000004u); return 0; }
    memcpy(media_path, new_path, path_len + 1);
    media_kind = kind;
    audio_ring_frames = 0;
    status_code = STATUS_RUNNING;
    error_code = 0;
    return 1;
}

__attribute__((export_name("pcfx_wasm_soft_reset")))
uint32_t pcfx_wasm_soft_reset(void)
{
    if(!emu || status_code != STATUS_RUNNING) { set_error(0xE0000005u); return 0; }
    if(!pcfx_headless_soft_reset(emu)) { set_error(0xE0000006u); return 0; }
    audio_ring_frames = 0;
    frame_counter = 0;
    error_code = 0;
    return 1;
}

__attribute__((export_name("pcfx_wasm_frame2")))
uint32_t pcfx_wasm_frame2(uint32_t buttons1, uint32_t buttons2, int32_t mouse_x, int32_t mouse_y, uint32_t mouse_buttons)
{
    if(!emu || status_code != STATUS_RUNNING) return 0;
    if(controller_type == 1u)
    {
        pcfx_headless_set_controller_type(emu, 1);
        pcfx_headless_set_mouse(emu, mouse_x, mouse_y, (uint16_t)(mouse_buttons & 0xFFFFu));
        pcfx_headless_set_pad(emu, 0, 0);
        pcfx_headless_set_pad(emu, 1, (uint16_t)buttons2);
    }
    else
    {
        pcfx_headless_set_controller_type(emu, 0);
        pcfx_headless_set_pad(emu, 0, (uint16_t)buttons1);
        pcfx_headless_set_pad(emu, 1, (uint16_t)buttons2);
        pcfx_headless_set_mouse(emu, 0, 0, 0);
    }
    if(!pcfx_headless_run_frame(emu)) { set_error(0xE0000003u); return 0; }
    frame_counter++;
    return 1;
}

__attribute__((export_name("pcfx_wasm_frame")))
uint32_t pcfx_wasm_frame(uint32_t buttons, int32_t mouse_x, int32_t mouse_y, uint32_t mouse_buttons)
{
    return pcfx_wasm_frame2(buttons, 0, mouse_x, mouse_y, mouse_buttons);
}

static void wasm_get_display_rect(int *out_x, int *out_y, int *out_w, int *out_h)
{
    int fbw = system_mode == 1u ? 344 : 256;
    int fbh = 240;
    int pitch = fbw;
    int fmt = 0, bpp = 0;
    if(emu)
        pcfx_headless_get_pixels(emu, &fbw, &fbh, &pitch, &fmt, &bpp);

    int x = 0, y = 0, rw = fbw, rh = fbh;
    if(emu)
        pcfx_headless_get_display_rect(emu, &x, &y, &rw, &rh);

    if(fbw <= 0) fbw = system_mode == 1u ? 344 : 256;
    if(fbh <= 0) fbh = 240;
    if(x < 0 || y < 0 || x >= fbw || y >= fbh || rw <= 0 || rh <= 0)
    {
        x = 0; y = 0; rw = fbw; rh = fbh;
    }
    if(rw > fbw - x) rw = fbw - x;
    if(rh > fbh - y) rh = fbh - y;
    if(rw <= 0) { x = 0; rw = fbw; }
    if(rh <= 0) { y = 0; rh = fbh; }

    if(out_x) *out_x = x;
    if(out_y) *out_y = y;
    if(out_w) *out_w = rw;
    if(out_h) *out_h = rh;
}

__attribute__((export_name("pcfx_wasm_get_framebuffer")))
uint32_t pcfx_wasm_get_framebuffer(void)
{
    int w = 0, h = 0, p = 0;
    int fmt = 0, bpp = 0;
    const uint8_t *fb = emu ? (const uint8_t*)pcfx_headless_get_pixels(emu, &w, &h, &p, &fmt, &bpp) : NULL;
    if(fb && bpp > 0 && p > 0)
    {
        int x = 0, y = 0, rw = 0, rh = 0;
        wasm_get_display_rect(&x, &y, &rw, &rh);
        (void)rw; (void)rh;
        return (uint32_t)(uintptr_t)(fb + ((size_t)y * (size_t)p + (size_t)x) * (size_t)bpp);
    }
    memset(fallback_fb, 0, sizeof(fallback_fb));
    return (uint32_t)(uintptr_t)fallback_fb;
}
__attribute__((export_name("pcfx_wasm_get_width"))) uint32_t pcfx_wasm_get_width(void){ int x=0,y=0,w=0,h=0; wasm_get_display_rect(&x,&y,&w,&h); return (uint32_t)w; }
__attribute__((export_name("pcfx_wasm_get_height"))) uint32_t pcfx_wasm_get_height(void){ int x=0,y=0,w=0,h=0; wasm_get_display_rect(&x,&y,&w,&h); return (uint32_t)h; }
__attribute__((export_name("pcfx_wasm_get_pitch_pixels"))) uint32_t pcfx_wasm_get_pitch_pixels(void){ int w=(system_mode==1u?344:256); int h=0; int p=(system_mode==1u?344:256); int fmt=0,bpp=0; if(emu) pcfx_headless_get_pixels(emu,&w,&h,&p,&fmt,&bpp); return (uint32_t)p; }
__attribute__((export_name("pcfx_wasm_get_pixel_format"))) uint32_t pcfx_wasm_get_pixel_format(void){ int w=0,h=0,p=0,fmt=0,bpp=0; if(emu) pcfx_headless_get_pixels(emu,&w,&h,&p,&fmt,&bpp); return (uint32_t)(fmt ? fmt : 2); }
__attribute__((export_name("pcfx_wasm_get_bytes_per_pixel"))) uint32_t pcfx_wasm_get_bytes_per_pixel(void){ int w=0,h=0,p=0,fmt=0,bpp=0; if(emu) pcfx_headless_get_pixels(emu,&w,&h,&p,&fmt,&bpp); return (uint32_t)(bpp ? bpp : 4); }
__attribute__((export_name("pcfx_wasm_get_status"))) uint32_t pcfx_wasm_get_status(void){ return status_code; }
__attribute__((export_name("pcfx_wasm_get_error"))) uint32_t pcfx_wasm_get_error(void){ return error_code; }
__attribute__((export_name("pcfx_wasm_get_frame_count"))) uint32_t pcfx_wasm_get_frame_count(void){ return emu ? (uint32_t)pcfx_headless_frame_count(emu) : frame_counter; }
__attribute__((export_name("pcfx_wasm_get_media_kind"))) uint32_t pcfx_wasm_get_media_kind(void){ return media_kind; }
__attribute__((export_name("pcfx_wasm_get_audio_rate"))) uint32_t pcfx_wasm_get_audio_rate(void){ return 44100; }
__attribute__((export_name("pcfx_wasm_get_audio_ptr"))) uint32_t pcfx_wasm_get_audio_ptr(void){ return (uint32_t)(uintptr_t)audio_ring; }
__attribute__((export_name("pcfx_wasm_get_audio_frames"))) uint32_t pcfx_wasm_get_audio_frames(void){ return audio_ring_frames; }
__attribute__((export_name("pcfx_wasm_audio_consume"))) void pcfx_wasm_audio_consume(uint32_t frames){ if(frames >= audio_ring_frames) { audio_ring_frames = 0; return; } memmove(audio_ring, audio_ring + frames * 2u, (audio_ring_frames - frames) * 2u * sizeof(audio_ring[0])); audio_ring_frames -= frames; }

__attribute__((export_name("pcfx_wasm_save_state")))
uint32_t pcfx_wasm_save_state(void)
{
    if(!emu) return 0;
    uint8_t *new_buf = NULL;
    size_t actual = 0;
    if(!PCFX_StateSerializeMalloc(&new_buf, &actual) || !new_buf || !actual || actual > 0xFFFFFFFFu)
    {
        free(new_buf);
        save_size = 0;
        return 0;
    }
    free(save_buf);
    save_buf = new_buf;
    save_size = (uint32_t)actual;
    save_capacity = save_size;
    return 1u;
}
__attribute__((export_name("pcfx_wasm_get_save_ptr"))) uint32_t pcfx_wasm_get_save_ptr(void){ return (uint32_t)(uintptr_t)save_buf; }
__attribute__((export_name("pcfx_wasm_get_save_size"))) uint32_t pcfx_wasm_get_save_size(void){ return save_size; }
__attribute__((export_name("pcfx_wasm_load_state"))) uint32_t pcfx_wasm_load_state(uint32_t ptr, uint32_t size){ return (ptr && size && PCFX_StateUnserialize((const void*)(uintptr_t)ptr, size)) ? 1u : 0u; }
