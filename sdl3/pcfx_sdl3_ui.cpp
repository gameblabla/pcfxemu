#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "headless/pcfx_headless.h"
#include "shell/menu/font_menudata.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const int EMU_W = 320;
static const int EMU_H = 240;
static const double PCFX_FPS = 60000.0 / 1001.0;

static std::string g_message;
static uint64_t g_message_until = 0;

static uint64_t now_ms()
{
    return SDL_GetTicks();
}

static void set_message(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_message = buf;
    g_message_until = now_ms() + 2500;
}

static bool file_exists(const std::string& p)
{
    struct stat st;
    return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool dir_exists(const std::string& p)
{
    struct stat st;
    return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool make_dir(const std::string& p)
{
    if(p.empty() || dir_exists(p)) return true;
    return mkdir(p.c_str(), 0775) == 0 || dir_exists(p);
}

static std::string path_join(const std::string& a, const std::string& b)
{
    if(a.empty()) return b;
    if(a[a.size() - 1] == '/' || a[a.size() - 1] == '\\') return a + b;
    return a + "/" + b;
}

static std::string basename_noext(const std::string& p)
{
    size_t s = p.find_last_of("/\\");
    std::string b = (s == std::string::npos) ? p : p.substr(s + 1);
    size_t d = b.find_last_of('.');
    if(d != std::string::npos && d > 0) b = b.substr(0, d);
    if(b.empty()) b = "pcfx";
    for(size_t i = 0; i < b.size(); i++)
    {
        char c = b[i];
        if(!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
            b[i] = '_';
    }
    return b;
}

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
}

static void set_color(SDL_Renderer* r, uint32_t c, float mul_alpha = 1.0f)
{
    uint8_t rr = (c >> 24) & 0xff;
    uint8_t gg = (c >> 16) & 0xff;
    uint8_t bb = (c >> 8) & 0xff;
    uint8_t aa = c & 0xff;
    aa = (uint8_t)std::max(0, std::min(255, (int)(aa * mul_alpha)));
    SDL_SetRenderDrawColor(r, rr, gg, bb, aa);
}

static void fill_rect(SDL_Renderer* r, float x, float y, float w, float h, uint32_t c, float a = 1.0f)
{
    SDL_FRect fr{ x, y, w, h };
    set_color(r, c, a);
    SDL_RenderFillRect(r, &fr);
}

static void stroke_rect(SDL_Renderer* r, float x, float y, float w, float h, uint32_t c, float a = 1.0f)
{
    SDL_FRect fr{ x, y, w, h };
    set_color(r, c, a);
    SDL_RenderRect(r, &fr);
}

static void draw_roundish_rect(SDL_Renderer* r, float x, float y, float w, float h, uint32_t c, float a = 1.0f)
{
    // SDL_Renderer has no primitive rounded rectangle in core SDL.  This approximates PPSSPP-style panels
    // without requiring SDL_ttf/gfx/image dependencies.
    fill_rect(r, x + 6, y, w - 12, h, c, a);
    fill_rect(r, x, y + 6, w, h - 12, c, a);
    fill_rect(r, x + 2, y + 2, w - 4, h - 4, c, a);
}

static int text_width(const std::string& s, int scale)
{
    return (int)s.size() * 8 * scale;
}

static void draw_text(SDL_Renderer* r, const std::string& text, float x, float y, uint32_t color, int scale = 2, float alpha = 1.0f)
{
    set_color(r, color, alpha);
    for(size_t i = 0; i < text.size(); i++)
    {
        unsigned char ch = (unsigned char)text[i];
        const uint8_t* glyph = &n2DLib_font[ch * 8];
        for(int gy = 0; gy < 8; gy++)
        {
            uint8_t row = glyph[gy];
            for(int gx = 0; gx < 8; gx++)
            {
                if(row & (0x80u >> gx))
                {
                    SDL_FRect p{ x + (float)((int)i * 8 * scale + gx * scale), y + (float)(gy * scale), (float)scale, (float)scale };
                    SDL_RenderFillRect(r, &p);
                }
            }
        }
    }
}

static void draw_text_shadow(SDL_Renderer* r, const std::string& text, float x, float y, uint32_t color, int scale = 2, float alpha = 1.0f)
{
    draw_text(r, text, x + scale, y + scale, rgba(0,0,0,220), scale, alpha * 0.85f);
    draw_text(r, text, x, y, color, scale, alpha);
}

static void draw_centered_text(SDL_Renderer* r, const std::string& text, float x, float y, float w, uint32_t color, int scale = 2, float alpha = 1.0f)
{
    float tx = x + (w - (float)text_width(text, scale)) * 0.5f;
    draw_text_shadow(r, text, tx, y, color, scale, alpha);
}

static void draw_gradient_bg(SDL_Renderer* r, int w, int h)
{
    for(int y = 0; y < h; y += 3)
    {
        const float t = (float)y / (float)std::max(1, h);
        uint8_t rr = (uint8_t)(9 + 14 * t);
        uint8_t gg = (uint8_t)(13 + 22 * t);
        uint8_t bb = (uint8_t)(22 + 45 * t);
        fill_rect(r, 0, (float)y, (float)w, 3.0f, rgba(rr, gg, bb, 245));
    }
}

static uint8_t rgb565_r(uint16_t p) { return (uint8_t)((((p >> 11) & 31) * 255 + 15) / 31); }
static uint8_t rgb565_g(uint16_t p) { return (uint8_t)((((p >> 5) & 63) * 255 + 31) / 63); }
static uint8_t rgb565_b(uint16_t p) { return (uint8_t)(((p & 31) * 255 + 15) / 31); }

static void put_be32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

static void append_chunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& data)
{
    put_be32(png, (uint32_t)data.size());
    size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, &png[start], (uInt)(png.size() - start));
    put_be32(png, (uint32_t)crc);
}

static bool write_png_rgb565(const std::string& path, const uint16_t* pix, int w, int h, int pitch)
{
    if(!pix || w <= 0 || h <= 0) return false;
    std::vector<uint8_t> raw;
    raw.resize((size_t)h * (1 + (size_t)w * 3));
    for(int y = 0; y < h; y++)
    {
        size_t row = (size_t)y * (1 + (size_t)w * 3);
        raw[row] = 0; // no filter
        for(int x = 0; x < w; x++)
        {
            uint16_t p = pix[y * pitch + x];
            raw[row + 1 + x * 3 + 0] = rgb565_r(p);
            raw[row + 1 + x * 3 + 1] = rgb565_g(p);
            raw[row + 1 + x * 3 + 2] = rgb565_b(p);
        }
    }

    uLongf comp_len = compressBound((uLong)raw.size());
    std::vector<uint8_t> comp(comp_len);
    if(compress2(comp.data(), &comp_len, raw.data(), (uLong)raw.size(), Z_BEST_SPEED) != Z_OK)
        return false;
    comp.resize(comp_len);

    std::vector<uint8_t> png;
    const uint8_t sig[8] = { 137,80,78,71,13,10,26,10 };
    png.insert(png.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr, (uint32_t)h);
    ihdr.push_back(8); // bit depth
    ihdr.push_back(2); // truecolor
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    append_chunk(png, "IHDR", ihdr);
    append_chunk(png, "IDAT", comp);
    std::vector<uint8_t> empty;
    append_chunk(png, "IEND", empty);

    FILE* fp = fopen(path.c_str(), "wb");
    if(!fp) return false;
    bool ok = fwrite(png.data(), 1, png.size(), fp) == png.size();
    fclose(fp);
    return ok;
}

static uint32_t read_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool load_own_png_rgb24(const std::string& path, std::vector<uint8_t>& rgb, int& w, int& h)
{
    rgb.clear(); w = h = 0;
    FILE* fp = fopen(path.c_str(), "rb");
    if(!fp) return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if(sz < 33 || sz > 16 * 1024 * 1024) { fclose(fp); return false; }
    std::vector<uint8_t> bytes((size_t)sz);
    if(fread(bytes.data(), 1, bytes.size(), fp) != bytes.size()) { fclose(fp); return false; }
    fclose(fp);
    const uint8_t sig[8] = { 137,80,78,71,13,10,26,10 };
    if(memcmp(bytes.data(), sig, 8)) return false;
    size_t off = 8;
    std::vector<uint8_t> idat;
    while(off + 12 <= bytes.size())
    {
        uint32_t len = read_be32(&bytes[off]); off += 4;
        if(off + 4 + len + 4 > bytes.size()) return false;
        char type[5]; memcpy(type, &bytes[off], 4); type[4] = 0; off += 4;
        const uint8_t* data = &bytes[off]; off += len;
        off += 4; // crc
        if(!strcmp(type, "IHDR"))
        {
            if(len < 13) return false;
            w = (int)read_be32(data); h = (int)read_be32(data + 4);
            if(data[8] != 8 || data[9] != 2 || data[10] || data[11] || data[12]) return false;
        }
        else if(!strcmp(type, "IDAT"))
            idat.insert(idat.end(), data, data + len);
        else if(!strcmp(type, "IEND"))
            break;
    }
    if(w <= 0 || h <= 0 || idat.empty()) return false;
    std::vector<uint8_t> raw((size_t)h * (1 + (size_t)w * 3));
    uLongf raw_len = (uLongf)raw.size();
    if(uncompress(raw.data(), &raw_len, idat.data(), (uLong)idat.size()) != Z_OK || raw_len != raw.size())
        return false;
    rgb.resize((size_t)w * (size_t)h * 3);
    for(int y = 0; y < h; y++)
    {
        size_t row = (size_t)y * (1 + (size_t)w * 3);
        if(raw[row] != 0) return false; // own writer uses filter 0
        memcpy(&rgb[(size_t)y * w * 3], &raw[row + 1], (size_t)w * 3);
    }
    return true;
}

struct Binding
{
    const char* name;
    uint16_t bit;
    SDL_Scancode scancode;
    SDL_GamepadButton gp_button;
};

struct HotkeyBinding
{
    const char* name;
    SDL_Scancode scancode;
};

enum HotkeyId
{
    HK_MENU = 0,
    HK_FULLSCREEN,
    HK_SAVE,
    HK_LOAD,
    HK_PREV_SLOT,
    HK_NEXT_SLOT,
    HK_SCREENSHOT,
    HK_COUNT
};

struct VideoOptions
{
    bool fullscreen = false;
    bool integer_scale = false;
    bool bilinear = true;
    bool scanlines = false;
    bool vsync = true;
    int aspect = 0; // 0=4:3, 1=native, 2=stretch
};

struct Preview
{
    SDL_Texture* texture = NULL;
    int w = 0;
    int h = 0;
};

struct App
{
    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;
    SDL_Texture* game_tex = NULL;
    SDL_AudioStream* audio_stream = NULL;
    SDL_Gamepad* gamepad = NULL;
    PCFX_Headless* emu = NULL;
    std::string game_path;
    std::string game_id;
    std::string bios_dir = ".";
    std::string save_dir = "saves";
    std::string state_dir;
    std::string cfg_path;
    VideoOptions video;
    std::vector<Binding> pad;
    HotkeyBinding hotkeys[HK_COUNT];
    bool running = true;
    bool menu = false;
    int tab = 0;
    int row = 0;
    int state_slot = 0;
    int remap_pad = -1;
    int remap_hotkey = -1;
    uint64_t start_ticks = 0;
    double next_frame_ms = 0.0;
    Preview previews[10];
};

static App* g_app = NULL;

static void audio_cb(void* userdata, const int16_t* samples, uint32_t frames)
{
    SDL_AudioStream* s = (SDL_AudioStream*)userdata;
    if(!s || !samples || !frames) return;
    // Keep latency bounded.  The stream queue is in bytes.
    const int queued = SDL_GetAudioStreamQueued(s);
    if(queued < 44100 * 4 / 2)
        SDL_PutAudioStreamData(s, samples, (int)(frames * 2u * sizeof(int16_t)));
}

static const char* scancode_name(SDL_Scancode sc)
{
    const char* n = SDL_GetScancodeName(sc);
    return (n && *n) ? n : "Unbound";
}

static SDL_Scancode scancode_from_name_safe(const char* n, SDL_Scancode fallback)
{
    if(!n || !*n) return fallback;
    SDL_Scancode s = SDL_GetScancodeFromName(n);
    return s == SDL_SCANCODE_UNKNOWN ? fallback : s;
}

static void init_bindings(App& a)
{
    a.pad.clear();
    a.pad.push_back({"Up", PCFX_PAD_UP, SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP});
    a.pad.push_back({"Down", PCFX_PAD_DOWN, SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN});
    a.pad.push_back({"Left", PCFX_PAD_LEFT, SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT});
    a.pad.push_back({"Right", PCFX_PAD_RIGHT, SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT});
    a.pad.push_back({"I / A", PCFX_PAD_A, SDL_SCANCODE_Z, SDL_GAMEPAD_BUTTON_SOUTH});
    a.pad.push_back({"II / B", PCFX_PAD_B, SDL_SCANCODE_X, SDL_GAMEPAD_BUTTON_EAST});
    a.pad.push_back({"III / C", PCFX_PAD_C, SDL_SCANCODE_C, SDL_GAMEPAD_BUTTON_WEST});
    a.pad.push_back({"IV / X", PCFX_PAD_X, SDL_SCANCODE_A, SDL_GAMEPAD_BUTTON_NORTH});
    a.pad.push_back({"V / Y", PCFX_PAD_Y, SDL_SCANCODE_S, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER});
    a.pad.push_back({"VI / Z", PCFX_PAD_Z, SDL_SCANCODE_D, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER});
    a.pad.push_back({"Start", PCFX_PAD_START, SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_START});
    a.pad.push_back({"Select", PCFX_PAD_SELECT, SDL_SCANCODE_RSHIFT, SDL_GAMEPAD_BUTTON_BACK});

    a.hotkeys[HK_MENU] = {"Menu", SDL_SCANCODE_F1};
    a.hotkeys[HK_FULLSCREEN] = {"Fullscreen", SDL_SCANCODE_F11};
    a.hotkeys[HK_SAVE] = {"Save state", SDL_SCANCODE_F5};
    a.hotkeys[HK_LOAD] = {"Load state", SDL_SCANCODE_F7};
    a.hotkeys[HK_PREV_SLOT] = {"Previous slot", SDL_SCANCODE_F6};
    a.hotkeys[HK_NEXT_SLOT] = {"Next slot", SDL_SCANCODE_F8};
    a.hotkeys[HK_SCREENSHOT] = {"Screenshot", SDL_SCANCODE_F12};
}

static bool parse_bool(const char* v, bool def)
{
    if(!v) return def;
    return !strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on");
}

static void save_config(App& a)
{
    FILE* fp = fopen(a.cfg_path.c_str(), "w");
    if(!fp) return;
    fprintf(fp, "fullscreen=%d\ninteger_scale=%d\nbilinear=%d\nscanlines=%d\nvsync=%d\naspect=%d\n",
            a.video.fullscreen ? 1 : 0, a.video.integer_scale ? 1 : 0, a.video.bilinear ? 1 : 0,
            a.video.scanlines ? 1 : 0, a.video.vsync ? 1 : 0, a.video.aspect);
    for(size_t i = 0; i < a.pad.size(); i++)
        fprintf(fp, "pad.%s=%s\n", a.pad[i].name, scancode_name(a.pad[i].scancode));
    for(int i = 0; i < HK_COUNT; i++)
        fprintf(fp, "hotkey.%s=%s\n", a.hotkeys[i].name, scancode_name(a.hotkeys[i].scancode));
    fclose(fp);
}

static void load_config(App& a)
{
    FILE* fp = fopen(a.cfg_path.c_str(), "r");
    if(!fp) return;
    char line[512];
    while(fgets(line, sizeof(line), fp))
    {
        char* nl = strchr(line, '\n'); if(nl) *nl = 0;
        char* eq = strchr(line, '='); if(!eq) continue;
        *eq++ = 0;
        const char* key = line;
        const char* val = eq;
        if(!strcmp(key, "fullscreen")) a.video.fullscreen = parse_bool(val, a.video.fullscreen);
        else if(!strcmp(key, "integer_scale")) a.video.integer_scale = parse_bool(val, a.video.integer_scale);
        else if(!strcmp(key, "bilinear")) a.video.bilinear = parse_bool(val, a.video.bilinear);
        else if(!strcmp(key, "scanlines")) a.video.scanlines = parse_bool(val, a.video.scanlines);
        else if(!strcmp(key, "vsync")) a.video.vsync = parse_bool(val, a.video.vsync);
        else if(!strcmp(key, "aspect")) a.video.aspect = atoi(val);
        else if(!strncmp(key, "pad.", 4))
        {
            const char* name = key + 4;
            for(size_t i = 0; i < a.pad.size(); i++)
                if(!strcmp(a.pad[i].name, name)) a.pad[i].scancode = scancode_from_name_safe(val, a.pad[i].scancode);
        }
        else if(!strncmp(key, "hotkey.", 7))
        {
            const char* name = key + 7;
            for(int i = 0; i < HK_COUNT; i++)
                if(!strcmp(a.hotkeys[i].name, name)) a.hotkeys[i].scancode = scancode_from_name_safe(val, a.hotkeys[i].scancode);
        }
    }
    fclose(fp);
    if(a.video.aspect < 0 || a.video.aspect > 2) a.video.aspect = 0;
}

static std::string slot_base(App& a, int slot)
{
    char b[64];
    snprintf(b, sizeof(b), "%s_slot%d", a.game_id.c_str(), slot);
    return path_join(a.state_dir, b);
}

static void destroy_previews(App& a)
{
    for(int i = 0; i < 10; i++)
    {
        if(a.previews[i].texture) SDL_DestroyTexture(a.previews[i].texture);
        a.previews[i] = Preview();
    }
}

static void load_preview(App& a, int slot)
{
    if(a.previews[slot].texture)
    {
        SDL_DestroyTexture(a.previews[slot].texture);
        a.previews[slot] = Preview();
    }
    std::vector<uint8_t> rgb;
    int w = 0, h = 0;
    if(!load_own_png_rgb24(slot_base(a, slot) + ".png", rgb, w, h))
        return;
    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGB24, rgb.data(), w * 3);
    if(!surf) return;
    a.previews[slot].texture = SDL_CreateTextureFromSurface(a.renderer, surf);
    a.previews[slot].w = w;
    a.previews[slot].h = h;
    SDL_DestroySurface(surf);
}

static void load_all_previews(App& a)
{
    destroy_previews(a);
    for(int i = 0; i < 10; i++) load_preview(a, i);
}

static void save_state_slot(App& a, int slot)
{
    std::string base = slot_base(a, slot);
    std::string state = base + ".mcr";
    std::string png = base + ".png";
    if(!pcfx_headless_save_state(a.emu, state.c_str()))
    {
        set_message("Save failed: %s", pcfx_headless_last_error(a.emu));
        return;
    }
    int w = 0, h = 0, pitch = 0;
    const uint16_t* pix = pcfx_headless_get_rgb565(a.emu, &w, &h, &pitch);
    if(!write_png_rgb565(png, pix, w, h, pitch))
        set_message("State saved, PNG preview failed");
    else
        set_message("Saved state slot %d", slot);
    load_preview(a, slot);
}

static void load_state_slot(App& a, int slot)
{
    std::string state = slot_base(a, slot) + ".mcr";
    if(!file_exists(state))
    {
        set_message("No state in slot %d", slot);
        return;
    }
    if(!pcfx_headless_load_state(a.emu, state.c_str()))
    {
        set_message("Load failed: %s", pcfx_headless_last_error(a.emu));
        return;
    }
    set_message("Loaded state slot %d", slot);
}

static void save_screenshot(App& a)
{
    char name[128];
    snprintf(name, sizeof(name), "%s_shot_%llu.png", a.game_id.c_str(), (unsigned long long)pcfx_headless_frame_count(a.emu));
    std::string p = path_join(a.save_dir, name);
    int w = 0, h = 0, pitch = 0;
    const uint16_t* pix = pcfx_headless_get_rgb565(a.emu, &w, &h, &pitch);
    if(write_png_rgb565(p, pix, w, h, pitch)) set_message("Screenshot saved: %s", name);
    else set_message("Screenshot failed");
}

static void apply_video_options(App& a)
{
    if(a.window) SDL_SetWindowFullscreen(a.window, a.video.fullscreen);
    if(a.renderer) SDL_SetRenderVSync(a.renderer, a.video.vsync ? 1 : 0);
    if(a.game_tex)
        SDL_SetTextureScaleMode(a.game_tex, a.video.bilinear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    save_config(a);
}

static uint16_t collect_pad_state(App& a, const bool* keys)
{
    uint16_t st = 0;
    for(size_t i = 0; i < a.pad.size(); i++)
    {
        if(a.pad[i].scancode != SDL_SCANCODE_UNKNOWN && keys[a.pad[i].scancode])
            st |= a.pad[i].bit;
        if(a.gamepad && a.pad[i].gp_button != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(a.gamepad, a.pad[i].gp_button))
            st |= a.pad[i].bit;
    }
    return st;
}

static bool is_hotkey(App& a, SDL_Scancode s, HotkeyId id)
{
    if(id == HK_MENU && s == SDL_SCANCODE_ESCAPE) return true;
    return a.hotkeys[id].scancode == s;
}

static void toggle_menu(App& a)
{
    a.menu = !a.menu;
    a.row = 0;
}

static void handle_hotkey(App& a, SDL_Scancode s)
{
    if(is_hotkey(a, s, HK_MENU)) { toggle_menu(a); return; }
    if(is_hotkey(a, s, HK_FULLSCREEN)) { a.video.fullscreen = !a.video.fullscreen; apply_video_options(a); set_message(a.video.fullscreen ? "Fullscreen" : "Windowed"); return; }
    if(is_hotkey(a, s, HK_SAVE)) { save_state_slot(a, a.state_slot); return; }
    if(is_hotkey(a, s, HK_LOAD)) { load_state_slot(a, a.state_slot); return; }
    if(is_hotkey(a, s, HK_PREV_SLOT)) { a.state_slot = (a.state_slot + 9) % 10; set_message("State slot %d", a.state_slot); return; }
    if(is_hotkey(a, s, HK_NEXT_SLOT)) { a.state_slot = (a.state_slot + 1) % 10; set_message("State slot %d", a.state_slot); return; }
    if(is_hotkey(a, s, HK_SCREENSHOT)) { save_screenshot(a); return; }
}

static int rows_for_tab(App& a)
{
    switch(a.tab)
    {
        case 0: return 7;
        case 1: return 6;
        case 2: return (int)a.pad.size();
        case 3: return 12;
        case 4: return HK_COUNT;
        default: return 6;
    }
}

static void adjust_menu(App& a, int dir)
{
    if(a.tab == 1)
    {
        switch(a.row)
        {
            case 0: a.video.fullscreen = !a.video.fullscreen; break;
            case 1: a.video.integer_scale = !a.video.integer_scale; break;
            case 2: a.video.aspect = (a.video.aspect + dir + 3) % 3; break;
            case 3: a.video.bilinear = !a.video.bilinear; break;
            case 4: a.video.scanlines = !a.video.scanlines; break;
            case 5: a.video.vsync = !a.video.vsync; break;
        }
        apply_video_options(a);
    }
    else if(a.tab == 3 && a.row == 0)
    {
        a.state_slot = (a.state_slot + dir + 10) % 10;
    }
}

static void activate_menu(App& a)
{
    if(a.tab == 1) { adjust_menu(a, 1); return; }
    if(a.tab == 2)
    {
        a.remap_pad = a.row;
        a.remap_hotkey = -1;
        set_message("Press a key for %s", a.pad[a.row].name);
        return;
    }
    if(a.tab == 3)
    {
        if(a.row == 1) save_state_slot(a, a.state_slot);
        else if(a.row == 2) load_state_slot(a, a.state_slot);
        else if(a.row >= 3 && a.row < 12) { a.state_slot = a.row - 3; }
        return;
    }
    if(a.tab == 4)
    {
        a.remap_hotkey = a.row;
        a.remap_pad = -1;
        set_message("Press a key for %s", a.hotkeys[a.row].name);
        return;
    }
}

static void handle_menu_key(App& a, SDL_Scancode s)
{
    if(a.remap_pad >= 0)
    {
        if(s != SDL_SCANCODE_ESCAPE)
        {
            a.pad[a.remap_pad].scancode = s;
            save_config(a);
            set_message("Mapped %s to %s", a.pad[a.remap_pad].name, scancode_name(s));
        }
        a.remap_pad = -1;
        return;
    }
    if(a.remap_hotkey >= 0)
    {
        if(s != SDL_SCANCODE_ESCAPE)
        {
            a.hotkeys[a.remap_hotkey].scancode = s;
            save_config(a);
            set_message("Mapped %s to %s", a.hotkeys[a.remap_hotkey].name, scancode_name(s));
        }
        a.remap_hotkey = -1;
        return;
    }

    if(is_hotkey(a, s, HK_MENU)) { toggle_menu(a); return; }
    if(s == SDL_SCANCODE_LEFT)
    {
        if(a.row == 0 && a.tab != 0) { a.tab--; a.row = 0; }
        else adjust_menu(a, -1);
    }
    else if(s == SDL_SCANCODE_RIGHT)
    {
        if(a.row == 0 && a.tab < 5) { a.tab++; a.row = 0; }
        else adjust_menu(a, 1);
    }
    else if(s == SDL_SCANCODE_UP) a.row = (a.row + rows_for_tab(a) - 1) % rows_for_tab(a);
    else if(s == SDL_SCANCODE_DOWN) a.row = (a.row + 1) % rows_for_tab(a);
    else if(s == SDL_SCANCODE_RETURN || s == SDL_SCANCODE_SPACE) activate_menu(a);
    else if(s == SDL_SCANCODE_F11) { a.video.fullscreen = !a.video.fullscreen; apply_video_options(a); }
}

static void handle_event(App& a, const SDL_Event& e)
{
    if(e.type == SDL_EVENT_QUIT) { a.running = false; return; }
    if(e.type == SDL_EVENT_GAMEPAD_ADDED && !a.gamepad)
        a.gamepad = SDL_OpenGamepad(e.gdevice.which);
    else if(e.type == SDL_EVENT_GAMEPAD_REMOVED)
    {
        if(a.gamepad && SDL_GetGamepadID(a.gamepad) == e.gdevice.which)
        {
            SDL_CloseGamepad(a.gamepad);
            a.gamepad = NULL;
        }
    }
    else if(e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
    {
        SDL_Scancode s = e.key.scancode;
        if(a.menu || a.remap_pad >= 0 || a.remap_hotkey >= 0) handle_menu_key(a, s);
        else handle_hotkey(a, s);
    }
}

static SDL_FRect compute_game_rect(App& a, int ww, int wh)
{
    float target_aspect = 4.0f / 3.0f;
    if(a.video.aspect == 1) target_aspect = (float)EMU_W / (float)EMU_H;
    if(a.video.aspect == 2) return SDL_FRect{0, 0, (float)ww, (float)wh};

    float dw = (float)ww;
    float dh = dw / target_aspect;
    if(dh > wh) { dh = (float)wh; dw = dh * target_aspect; }
    if(a.video.integer_scale)
    {
        int sx = std::max(1, (int)(dw / EMU_W));
        int sy = std::max(1, (int)(dh / EMU_H));
        int s = std::min(sx, sy);
        dw = (float)(EMU_W * s);
        dh = (float)(EMU_H * s);
    }
    return SDL_FRect{ ((float)ww - dw) * 0.5f, ((float)wh - dh) * 0.5f, dw, dh };
}

static const char* aspect_name(int a)
{
    return a == 0 ? "4:3" : (a == 1 ? "Native" : "Stretch");
}

static const char* onoff(bool b) { return b ? "On" : "Off"; }

static void menu_row(SDL_Renderer* r, App& a, int idx, float x, float y, float w, const std::string& left, const std::string& right = "")
{
    const bool sel = a.row == idx;
    if(sel)
    {
        draw_roundish_rect(r, x - 8, y - 5, w + 16, 32, rgba(32, 112, 190, 210));
        stroke_rect(r, x - 8, y - 5, w + 16, 32, rgba(105, 210, 255, 230));
    }
    draw_text_shadow(r, left, x, y, sel ? rgba(255,255,255,255) : rgba(205,222,238,255), 2);
    if(!right.empty())
    {
        int tw = text_width(right, 2);
        draw_text_shadow(r, right, x + w - tw, y, rgba(123, 223, 255,255), 2);
    }
}

static void draw_preview(SDL_Renderer* r, App& a, int slot, float x, float y, float w, float h)
{
    draw_roundish_rect(r, x, y, w, h, rgba(0,0,0,180));
    stroke_rect(r, x, y, w, h, rgba(105,210,255,180));
    if(a.previews[slot].texture)
    {
        SDL_FRect dst{ x + 8, y + 8, w - 16, h - 16 };
        SDL_RenderTexture(r, a.previews[slot].texture, NULL, &dst);
    }
    else draw_centered_text(r, "EMPTY", x, y + h * 0.42f, w, rgba(120,135,150,255), 2);
}

static void render_menu(App& a, int ww, int wh)
{
    SDL_Renderer* r = a.renderer;
    fill_rect(r, 0, 0, (float)ww, (float)wh, rgba(0,0,0,115));
    float x = std::max(30.0f, ww * 0.08f);
    float y = std::max(30.0f, wh * 0.08f);
    float w = ww - 2*x;
    float h = wh - 2*y;
    draw_roundish_rect(r, x, y, w, h, rgba(13, 22, 37, 230));
    stroke_rect(r, x, y, w, h, rgba(88, 205, 255, 180));
    fill_rect(r, x, y, w, 56, rgba(22, 70, 122, 220));
    draw_text_shadow(r, "PC-FX / PC-FXGA", x + 24, y + 16, rgba(255,255,255,255), 3);
    draw_text_shadow(r, "F1 / ESC closes menu", x + w - 290, y + 22, rgba(170,220,255,255), 2);

    const char* tabs[] = { "Game", "Video", "Input", "States", "Hotkeys", "About" };
    float side_x = x + 18;
    float side_y = y + 78;
    float side_w = 160;
    for(int i = 0; i < 6; i++)
    {
        if(i == a.tab) draw_roundish_rect(r, side_x, side_y + i * 42, side_w, 32, rgba(28, 120, 198, 230));
        draw_text_shadow(r, tabs[i], side_x + 14, side_y + i * 42 + 8, i == a.tab ? rgba(255,255,255,255) : rgba(168,190,210,255), 2);
    }

    float cx = side_x + side_w + 32;
    float cy = side_y;
    float cw = x + w - cx - 24;
    draw_roundish_rect(r, cx - 12, cy - 12, cw + 24, h - 96, rgba(6, 10, 18, 160));

    if(a.tab == 0)
    {
        menu_row(r, a, 0, cx, cy, cw, "Running", a.game_id);
        menu_row(r, a, 1, cx, cy + 38, cw, "Frame", std::to_string((unsigned long long)pcfx_headless_frame_count(a.emu)));
        menu_row(r, a, 2, cx, cy + 76, cw, "Active state slot", std::to_string(a.state_slot));
        menu_row(r, a, 3, cx, cy + 114, cw, "Save state now", "Enter");
        menu_row(r, a, 4, cx, cy + 152, cw, "Load state now", "Enter");
        menu_row(r, a, 5, cx, cy + 190, cw, "Screenshot", "F12");
        menu_row(r, a, 6, cx, cy + 228, cw, "Quit", "Close window");
    }
    else if(a.tab == 1)
    {
        menu_row(r, a, 0, cx, cy, cw, "Fullscreen", onoff(a.video.fullscreen));
        menu_row(r, a, 1, cx, cy + 38, cw, "Integer scaling", onoff(a.video.integer_scale));
        menu_row(r, a, 2, cx, cy + 76, cw, "Aspect ratio", aspect_name(a.video.aspect));
        menu_row(r, a, 3, cx, cy + 114, cw, "Bilinear filter", onoff(a.video.bilinear));
        menu_row(r, a, 4, cx, cy + 152, cw, "Subtle scanlines", onoff(a.video.scanlines));
        menu_row(r, a, 5, cx, cy + 190, cw, "VSync", onoff(a.video.vsync));
    }
    else if(a.tab == 2)
    {
        for(size_t i = 0; i < a.pad.size(); i++)
            menu_row(r, a, (int)i, cx, cy + i * 32, cw, a.pad[i].name, scancode_name(a.pad[i].scancode));
        if(a.remap_pad >= 0)
            draw_centered_text(r, "Press a replacement key", cx, y + h - 50, cw, rgba(255,230,120,255), 2);
    }
    else if(a.tab == 3)
    {
        menu_row(r, a, 0, cx, cy, cw, "Current slot", std::to_string(a.state_slot));
        menu_row(r, a, 1, cx, cy + 36, cw, "Save current slot", "PNG preview");
        menu_row(r, a, 2, cx, cy + 72, cw, "Load current slot", "Enter");
        float px = cx;
        float py = cy + 126;
        for(int i = 0; i < 9; i++)
        {
            int slot = i;
            float sx = px + (i % 3) * 150;
            float sy = py + (i / 3) * 104;
            int oldrow = a.row;
            if(a.row == 3 + slot) stroke_rect(r, sx - 4, sy - 4, 132, 92, rgba(255,255,140,255));
            draw_preview(r, a, slot, sx, sy, 124, 84);
            char label[32]; snprintf(label, sizeof(label), "Slot %d", slot);
            draw_text_shadow(r, label, sx + 18, sy + 90, rgba(220,230,240,255), 1);
            a.row = oldrow;
        }
    }
    else if(a.tab == 4)
    {
        for(int i = 0; i < HK_COUNT; i++)
            menu_row(r, a, i, cx, cy + i * 36, cw, a.hotkeys[i].name, scancode_name(a.hotkeys[i].scancode));
        if(a.remap_hotkey >= 0)
            draw_centered_text(r, "Press a replacement hotkey", cx, y + h - 50, cw, rgba(255,230,120,255), 2);
    }
    else
    {
        draw_text_shadow(r, "SDL3 frontend", cx, cy, rgba(255,255,255,255), 3);
        draw_text_shadow(r, "PPSSPP-style overlay UI: hidden by default, non-modal during gameplay.", cx, cy + 48, rgba(205,220,235,255), 2);
        draw_text_shadow(r, "Save states write a .mcr state and a .png preview side-by-side.", cx, cy + 82, rgba(205,220,235,255), 2);
        draw_text_shadow(r, "Keyboard and gamepad buttons are configurable from Input/Hotkeys.", cx, cy + 116, rgba(205,220,235,255), 2);
        draw_text_shadow(r, "No OwlResampler path is used by this frontend.", cx, cy + 150, rgba(205,220,235,255), 2);
    }
}

static void render_prompt(App& a, int ww)
{
    uint64_t t = now_ms() - a.start_ticks;
    float alpha = 1.0f;
    if(t > 1500)
    {
        if(t >= 2300) return;
        alpha = 1.0f - (float)(t - 1500) / 800.0f;
    }
    const std::string s = "Press F1 / ESC for menu";
    float bw = (float)text_width(s, 2) + 44;
    float x = (ww - bw) * 0.5f;
    float y = 28;
    draw_roundish_rect(a.renderer, x, y, bw, 42, rgba(0,0,0,190), alpha);
    stroke_rect(a.renderer, x, y, bw, 42, rgba(90,210,255,180), alpha);
    draw_centered_text(a.renderer, s, x, y + 12, bw, rgba(255,255,255,255), 2, alpha);
}

static void render_transient_message(App& a, int ww, int wh)
{
    if(g_message.empty() || now_ms() > g_message_until) return;
    float bw = (float)text_width(g_message, 2) + 36;
    float x = (ww - bw) * 0.5f;
    float y = wh - 72.0f;
    draw_roundish_rect(a.renderer, x, y, bw, 38, rgba(0,0,0,205));
    draw_centered_text(a.renderer, g_message, x, y + 10, bw, rgba(255,255,255,255), 2);
}

static void render_game(App& a)
{
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(a.window, &ww, &wh);
    if(ww <= 0 || wh <= 0) { ww = 960; wh = 720; }

    int w = 0, h = 0, pitch = 0;
    const uint16_t* pix = pcfx_headless_get_rgb565(a.emu, &w, &h, &pitch);
    if(pix)
        SDL_UpdateTexture(a.game_tex, NULL, pix, pitch * (int)sizeof(uint16_t));

    draw_gradient_bg(a.renderer, ww, wh);
    SDL_FRect dst = compute_game_rect(a, ww, wh);
    SDL_RenderTexture(a.renderer, a.game_tex, NULL, &dst);
    if(a.video.scanlines)
    {
        for(int y = 0; y < (int)dst.h; y += 2)
            fill_rect(a.renderer, dst.x, dst.y + y, dst.w, 1, rgba(0,0,0,75));
    }
    render_prompt(a, ww);
    if(a.menu) render_menu(a, ww, wh);
    render_transient_message(a, ww, wh);
    SDL_RenderPresent(a.renderer);
}

static void print_usage(const char* argv0)
{
    fprintf(stderr, "Usage: %s [--bios-dir DIR] [--save-dir DIR] [--fast-video] [--fullscreen] game.cue|game.chd|homebrew_dir|program.EX\n", argv0);
}

int main(int argc, char** argv)
{
    App app;
    g_app = &app;
    init_bindings(app);

    int fast_video = 0;
    for(int i = 1; i < argc; i++)
    {
        if(!strcmp(argv[i], "--bios-dir") && i + 1 < argc) app.bios_dir = argv[++i];
        else if(!strcmp(argv[i], "--save-dir") && i + 1 < argc) app.save_dir = argv[++i];
        else if(!strcmp(argv[i], "--fast-video")) fast_video = 1;
        else if(!strcmp(argv[i], "--fullscreen")) app.video.fullscreen = true;
        else if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { print_usage(argv[0]); return 0; }
        else app.game_path = argv[i];
    }
    if(app.game_path.empty()) { print_usage(argv[0]); return 1; }
    app.game_id = basename_noext(app.game_path);
    make_dir(app.save_dir);
    app.state_dir = path_join(app.save_dir, "states");
    make_dir(app.state_dir);
    app.cfg_path = path_join(app.save_dir, "pcfx_sdl3_ui.cfg");
    load_config(app);

    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_VSYNC, app.video.vsync ? "1" : "0");
    app.window = SDL_CreateWindow("PC-FX / PC-FXGA", 1280, 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(!app.window)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit(); return 1;
    }
    app.renderer = SDL_CreateRenderer(app.window, NULL);
    if(!app.renderer)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(app.window); SDL_Quit(); return 1;
    }
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderVSync(app.renderer, app.video.vsync ? 1 : 0);
    app.game_tex = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, EMU_W, EMU_H);
    if(!app.game_tex)
    {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_Quit(); return 1;
    }
    SDL_SetTextureScaleMode(app.game_tex, app.video.bilinear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 44100;
    app.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if(app.audio_stream)
        SDL_ResumeAudioStreamDevice(app.audio_stream);

    PCFX_HeadlessConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.bios_dir = app.bios_dir.c_str();
    cfg.save_dir = app.save_dir.c_str();
    cfg.sound_rate = 44100;
    cfg.fast_video = fast_video;
    app.emu = pcfx_headless_create(&cfg);
    if(!app.emu)
    {
        fprintf(stderr, "pcfx_headless_create failed\n");
        SDL_Quit(); return 1;
    }
    pcfx_headless_set_audio_callback(app.emu, audio_cb, app.audio_stream);
    if(!pcfx_headless_load_cd(app.emu, app.game_path.c_str()))
    {
        fprintf(stderr, "load failed: %s\n", pcfx_headless_last_error(app.emu));
        pcfx_headless_destroy(app.emu); SDL_Quit(); return 1;
    }

    apply_video_options(app);
    load_all_previews(app);
    app.start_ticks = now_ms();
    app.next_frame_ms = (double)app.start_ticks;

    if(SDL_GetNumGamepads() > 0)
    {
        SDL_JoystickID* ids = SDL_GetGamepads(NULL);
        if(ids)
        {
            app.gamepad = SDL_OpenGamepad(ids[0]);
            SDL_free(ids);
        }
    }

    while(app.running)
    {
        SDL_Event e;
        while(SDL_PollEvent(&e)) handle_event(app, e);

        const bool* keys = SDL_GetKeyboardState(NULL);
        if(!app.menu && app.remap_pad < 0 && app.remap_hotkey < 0)
            pcfx_headless_set_pad(app.emu, 0, collect_pad_state(app, keys));
        else
            pcfx_headless_set_pad(app.emu, 0, 0);

        double t = (double)now_ms();
        if(!app.menu)
        {
            int ran = 0;
            while(t + 1.0 >= app.next_frame_ms && ran < 4)
            {
                if(!pcfx_headless_run_frame(app.emu))
                {
                    set_message("Emulation error: %s", pcfx_headless_last_error(app.emu));
                    app.running = false;
                    break;
                }
                app.next_frame_ms += 1000.0 / PCFX_FPS;
                ran++;
            }
            if(ran == 4 && t > app.next_frame_ms + 500.0) app.next_frame_ms = t;
        }
        else app.next_frame_ms = t + 1000.0 / PCFX_FPS;

        render_game(app);
        SDL_Delay(1);
    }

    save_config(app);
    destroy_previews(app);
    if(app.gamepad) SDL_CloseGamepad(app.gamepad);
    if(app.audio_stream) SDL_DestroyAudioStream(app.audio_stream);
    if(app.game_tex) SDL_DestroyTexture(app.game_tex);
    if(app.emu) pcfx_headless_destroy(app.emu);
    if(app.renderer) SDL_DestroyRenderer(app.renderer);
    if(app.window) SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
