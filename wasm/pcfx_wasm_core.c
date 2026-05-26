/*
 * Minimal freestanding WebAssembly host backend for the PC-FX emulator web UI.
 *
 * This file intentionally uses only clang's raw wasm32 output.  It does not use
 * Emscripten, WASI, libc, files, or browser-specific imports.  The JavaScript
 * frontend owns file pickers, LocalStorage, audio/canvas plumbing, and copies
 * selected BIOS/media bytes or metadata into this module's linear memory.
 *
 * The exported ABI is deliberately small so the native PC-FX core can be wired
 * behind it without changing the web UI.
 */

#include <stdint.h>
#include <stddef.h>

#define PCFX_WASM_MAX_W 512u
#define PCFX_WASM_MAX_H 240u
#define PCFX_WASM_HEAP_SIZE (96u * 1024u * 1024u)
#define PCFX_WASM_SAVE_SIZE 256u

#define BUTTON_A      (1u << 0)
#define BUTTON_B      (1u << 1)
#define BUTTON_C      (1u << 2)
#define BUTTON_X      (1u << 3)
#define BUTTON_Y      (1u << 4)
#define BUTTON_Z      (1u << 5)
#define BUTTON_SELECT (1u << 6)
#define BUTTON_START  (1u << 7)
#define BUTTON_UP     (1u << 8)
#define BUTTON_RIGHT  (1u << 9)
#define BUTTON_DOWN   (1u << 10)
#define BUTTON_LEFT   (1u << 11)

#define STATUS_NO_BIOS 0u
#define STATUS_BIOS_READY 1u
#define STATUS_MEDIA_READY 2u
#define STATUS_RUNNING 3u
#define STATUS_PAUSED 4u

static uint16_t fb[PCFX_WASM_MAX_W * PCFX_WASM_MAX_H];
static uint8_t heap[PCFX_WASM_HEAP_SIZE];
static uint8_t save_blob[PCFX_WASM_SAVE_SIZE];

static uint32_t heap_top;
static uint32_t frame_counter;
static uint32_t system_mode; /* 0 = PC-FX, 1 = PC-FXGA */
static uint32_t status_code;
static uint32_t fb_w;
static uint32_t fb_h;
static uint32_t bios_size;
static uint32_t bios_checksum;
static uint32_t bios_kind;
static uint32_t media_size_lo;
static uint32_t media_checksum;
static uint32_t media_kind;
static uint32_t last_buttons;
static uint32_t paused;

static uint32_t align16(uint32_t v)
{
    return (v + 15u) & ~15u;
}

__attribute__((export_name("pcfx_wasm_malloc")))
uint32_t pcfx_wasm_malloc(uint32_t size)
{
    size = align16(size ? size : 1u);
    if(heap_top + size > PCFX_WASM_HEAP_SIZE)
        return 0u;
    uint32_t ret = (uint32_t)(uintptr_t)(heap + heap_top);
    heap_top += size;
    return ret;
}

__attribute__((export_name("pcfx_wasm_reset_heap")))
void pcfx_wasm_reset_heap(void)
{
    heap_top = 0u;
}

static uint32_t fnv1a(const uint8_t* p, uint32_t size)
{
    uint32_t h = 2166136261u;
    for(uint32_t i = 0; i < size; i++)
    {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
}

static void clear(uint16_t c)
{
    const uint32_t n = fb_w * fb_h;
    for(uint32_t i = 0; i < n; i++)
        fb[i] = c;
}

static void rect(int x, int y, int w, int h, uint16_t c)
{
    if(w <= 0 || h <= 0)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if(x1 > (int)fb_w) x1 = (int)fb_w;
    if(y1 > (int)fb_h) y1 = (int)fb_h;
    for(int yy = y0; yy < y1; yy++)
        for(int xx = x0; xx < x1; xx++)
            fb[(uint32_t)yy * fb_w + (uint32_t)xx] = c;
}

static void line_h(int x, int y, int w, uint16_t c)
{
    rect(x, y, w, 1, c);
}

static void line_v(int x, int y, int h, uint16_t c)
{
    rect(x, y, 1, h, c);
}

static void border(int x, int y, int w, int h, uint16_t c)
{
    line_h(x, y, w, c);
    line_h(x, y + h - 1, w, c);
    line_v(x, y, h, c);
    line_v(x + w - 1, y, h, c);
}

/* 3x5 debug glyphs.  Bits are left to right, top to bottom. */
static uint16_t glyph3x5(char ch)
{
    switch(ch)
    {
        case '0': return 0x7b6fu;
        case '1': return 0x2492u;
        case '2': return 0x73e7u;
        case '3': return 0x73cfu;
        case '4': return 0x5bc9u;
        case '5': return 0x79cfu;
        case '6': return 0x79efu;
        case '7': return 0x7249u;
        case '8': return 0x7befu;
        case '9': return 0x7bcfu;
        case 'A': return 0x5befu;
        case 'B': return 0x7aefu;
        case 'C': return 0x7927u;
        case 'D': return 0x6b6eu;
        case 'E': return 0x79e7u;
        case 'F': return 0x79e4u;
        case 'G': return 0x79afu;
        case 'H': return 0x5be9u;
        case 'I': return 0x7497u;
        case 'J': return 0x1257u;
        case 'K': return 0x5ac9u;
        case 'L': return 0x4927u;
        case 'M': return 0x5ff9u;
        case 'N': return 0x5fedu;
        case 'O': return 0x5b6du;
        case 'P': return 0x5be4u;
        case 'Q': return 0x5b7bu;
        case 'R': return 0x5bebu;
        case 'S': return 0x79cfu;
        case 'T': return 0x7492u;
        case 'U': return 0x5b6fu;
        case 'V': return 0x5b54u;
        case 'W': return 0x5fffu;
        case 'X': return 0x5aadu;
        case 'Y': return 0x5b92u;
        case 'Z': return 0x72a7u;
        case '-': return 0x01c0u;
        case '.': return 0x0002u;
        case ':': return 0x0208u;
        case '/': return 0x1248u;
        case ' ': return 0x0000u;
        default: return 0x7defu;
    }
}

static void draw_char(int x, int y, char ch, int scale, uint16_t fg, uint16_t bg, int opaque)
{
    uint16_t bits = glyph3x5(ch);
    for(int yy = 0; yy < 5; yy++)
    {
        for(int xx = 0; xx < 3; xx++)
        {
            int bit = 14 - (yy * 3 + xx);
            int on = (bits >> bit) & 1;
            if(on)
                rect(x + xx * scale, y + yy * scale, scale, scale, fg);
            else if(opaque)
                rect(x + xx * scale, y + yy * scale, scale, scale, bg);
        }
    }
}

static void draw_text(int x, int y, const char* s, int scale, uint16_t fg)
{
    for(int i = 0; s[i]; i++)
    {
        draw_char(x, y, s[i], scale, fg, 0, 0);
        x += 4 * scale;
    }
}

static void draw_hex8(int x, int y, uint32_t v, int scale, uint16_t fg)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[9];
    for(int i = 0; i < 8; i++)
        buf[i] = hex[(v >> ((7 - i) * 4)) & 0xf];
    buf[8] = 0;
    draw_text(x, y, buf, scale, fg);
}

static void draw_scene(void)
{
    uint16_t black = rgb565(4, 8, 18);
    uint16_t navy = rgb565(10, 25, 58);
    uint16_t blue = rgb565(28, 68, 160);
    uint16_t cyan = rgb565(72, 204, 224);
    uint16_t white = rgb565(235, 239, 232);
    uint16_t amber = rgb565(245, 172, 32);
    uint16_t green = rgb565(92, 220, 92);
    uint16_t red = rgb565(230, 74, 74);
    uint16_t gray = rgb565(126, 136, 148);

    clear(black);

    for(uint32_t y = 0; y < fb_h; y++)
    {
        uint8_t r = (uint8_t)(8 + ((y * 20) / fb_h));
        uint8_t g = (uint8_t)(18 + ((y * 34) / fb_h));
        uint8_t b = (uint8_t)(46 + ((y * 94) / fb_h));
        uint16_t c = rgb565(r, g, b);
        for(uint32_t x = 0; x < fb_w; x++)
            fb[y * fb_w + x] = c;
    }

    for(uint32_t x = 0; x < fb_w; x += 16)
    {
        uint16_t c = (x & 16u) ? navy : blue;
        rect((int)x, 0, 2, (int)fb_h, c);
    }

    border(8, 8, (int)fb_w - 16, (int)fb_h - 16, cyan);
    rect(18, 22, (int)fb_w - 36, 38, rgb565(18, 28, 70));
    border(18, 22, (int)fb_w - 36, 38, blue);

    draw_text(28, 32, system_mode ? "PC-FXGA WASM" : "PC-FX WASM", 3, white);
    draw_text(28, 68, "CLANG NO EMSCRIPTEN", 2, cyan);

    if(status_code == STATUS_NO_BIOS)
    {
        draw_text(28, 98, "LOAD BIOS", 3, amber);
        draw_text(28, 132, "PCFX OR PCFXGA", 2, gray);
    }
    else
    {
        draw_text(28, 96, "BIOS READY", 3, green);
        draw_text(28, 130, "BIOS FNV", 2, gray);
        draw_hex8(120, 130, bios_checksum, 2, white);
    }

    if(status_code >= STATUS_MEDIA_READY)
    {
        draw_text(28, 154, "MEDIA READY", 2, green);
        draw_hex8(148, 154, media_checksum, 2, white);
    }
    else
    {
        draw_text(28, 154, "DROP DISC OR HUEXE", 2, amber);
    }

    draw_text(28, 198, paused ? "PAUSED" : "RUN", 2, paused ? red : green);
    draw_text(124, 198, "FRAME", 2, gray);
    draw_hex8(212, 198, frame_counter, 2, white);

    int cx = (int)((frame_counter * 3u) % (fb_w - 44u)) + 22;
    int cy = 176;
    rect(cx, cy, 22, 8, amber);
    rect(cx + 6, cy - 7, 18, 7, rgb565(252, 214, 42));
    rect(cx + 19, cy + 2, 14, 5, rgb565(220, 72, 30));

    if(last_buttons & BUTTON_UP) rect((int)fb_w - 58, 132, 16, 16, green);
    if(last_buttons & BUTTON_DOWN) rect((int)fb_w - 58, 168, 16, 16, green);
    if(last_buttons & BUTTON_LEFT) rect((int)fb_w - 76, 150, 16, 16, green);
    if(last_buttons & BUTTON_RIGHT) rect((int)fb_w - 40, 150, 16, 16, green);
    if(last_buttons & BUTTON_A) rect((int)fb_w - 116, 144, 18, 18, amber);
    if(last_buttons & BUTTON_B) rect((int)fb_w - 92, 144, 18, 18, red);
}

__attribute__((export_name("pcfx_wasm_init")))
void pcfx_wasm_init(uint32_t mode)
{
    system_mode = mode ? 1u : 0u;
    frame_counter = 0u;
    status_code = STATUS_NO_BIOS;
    bios_size = bios_checksum = bios_kind = 0u;
    media_size_lo = media_checksum = media_kind = 0u;
    last_buttons = 0u;
    paused = 0u;
    fb_w = system_mode ? 344u : 256u;
    fb_h = 240u;
    pcfx_wasm_reset_heap();
    draw_scene();
}

__attribute__((export_name("pcfx_wasm_set_system_mode")))
void pcfx_wasm_set_system_mode(uint32_t mode)
{
    system_mode = mode ? 1u : 0u;
    fb_w = system_mode ? 344u : 256u;
    draw_scene();
}

__attribute__((export_name("pcfx_wasm_load_bios")))
uint32_t pcfx_wasm_load_bios(uint32_t ptr, uint32_t size, uint32_t kind)
{
    if(!ptr || size < 256u)
        return 0u;
    bios_size = size;
    bios_kind = kind;
    bios_checksum = fnv1a((const uint8_t*)(uintptr_t)ptr, size > (2u * 1024u * 1024u) ? (2u * 1024u * 1024u) : size);
    status_code = STATUS_BIOS_READY;
    draw_scene();
    return 1u;
}

__attribute__((export_name("pcfx_wasm_set_media")))
uint32_t pcfx_wasm_set_media(uint32_t ptr, uint32_t size, uint32_t total_size, uint32_t kind)
{
    media_kind = kind;
    media_size_lo = total_size;
    if(ptr && size)
        media_checksum = fnv1a((const uint8_t*)(uintptr_t)ptr, size);
    else
        media_checksum = total_size ^ (kind * 0x9e3779b9u);
    if(status_code >= STATUS_BIOS_READY)
        status_code = STATUS_MEDIA_READY;
    draw_scene();
    return 1u;
}

__attribute__((export_name("pcfx_wasm_frame")))
void pcfx_wasm_frame(uint32_t buttons, int32_t mouse_x, int32_t mouse_y, uint32_t mouse_buttons)
{
    (void)mouse_x;
    (void)mouse_y;
    if(mouse_buttons & 2u)
        paused = 1u;
    if(mouse_buttons & 4u)
        paused = 0u;
    last_buttons = buttons;
    if(!paused)
    {
        frame_counter++;
        if(status_code == STATUS_MEDIA_READY)
            status_code = STATUS_RUNNING;
    }
    draw_scene();
}

__attribute__((export_name("pcfx_wasm_get_framebuffer")))
uint32_t pcfx_wasm_get_framebuffer(void)
{
    return (uint32_t)(uintptr_t)fb;
}

__attribute__((export_name("pcfx_wasm_get_width")))
uint32_t pcfx_wasm_get_width(void) { return fb_w; }

__attribute__((export_name("pcfx_wasm_get_height")))
uint32_t pcfx_wasm_get_height(void) { return fb_h; }

__attribute__((export_name("pcfx_wasm_get_pitch_pixels")))
uint32_t pcfx_wasm_get_pitch_pixels(void) { return fb_w; }

__attribute__((export_name("pcfx_wasm_get_status")))
uint32_t pcfx_wasm_get_status(void) { return status_code; }

__attribute__((export_name("pcfx_wasm_get_frame_count")))
uint32_t pcfx_wasm_get_frame_count(void) { return frame_counter; }

__attribute__((export_name("pcfx_wasm_get_save_ptr")))
uint32_t pcfx_wasm_get_save_ptr(void) { return (uint32_t)(uintptr_t)save_blob; }

__attribute__((export_name("pcfx_wasm_get_save_size")))
uint32_t pcfx_wasm_get_save_size(void) { return PCFX_WASM_SAVE_SIZE; }

__attribute__((export_name("pcfx_wasm_save_state")))
uint32_t pcfx_wasm_save_state(void)
{
    for(uint32_t i = 0; i < PCFX_WASM_SAVE_SIZE; i++)
        save_blob[i] = 0u;
    save_blob[0] = 'P'; save_blob[1] = 'F'; save_blob[2] = 'X'; save_blob[3] = 'S';
    uint32_t* w = (uint32_t*)(void*)save_blob;
    w[1] = 1u;
    w[2] = frame_counter;
    w[3] = system_mode;
    w[4] = status_code;
    w[5] = bios_checksum;
    w[6] = media_checksum;
    w[7] = last_buttons;
    return 1u;
}

__attribute__((export_name("pcfx_wasm_load_state")))
uint32_t pcfx_wasm_load_state(uint32_t ptr, uint32_t size)
{
    if(!ptr || size < 32u)
        return 0u;
    const uint8_t* p = (const uint8_t*)(uintptr_t)ptr;
    if(p[0] != 'P' || p[1] != 'F' || p[2] != 'X' || p[3] != 'S')
        return 0u;
    const uint32_t* w = (const uint32_t*)(const void*)p;
    frame_counter = w[2];
    system_mode = w[3] ? 1u : 0u;
    status_code = w[4];
    bios_checksum = w[5];
    media_checksum = w[6];
    last_buttons = w[7];
    fb_w = system_mode ? 344u : 256u;
    fb_h = 240u;
    draw_scene();
    return 1u;
}

__attribute__((export_name("pcfx_wasm_pause")))
void pcfx_wasm_pause(uint32_t p)
{
    paused = p ? 1u : 0u;
    draw_scene();
}

__attribute__((export_name("pcfx_wasm_version")))
uint32_t pcfx_wasm_version(void)
{
    return 0x00010000u;
}
