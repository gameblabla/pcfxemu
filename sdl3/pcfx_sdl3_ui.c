#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_dialog.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <zlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "headless/pcfx_headless.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define EMU_W 512
#define EMU_H 240
#define PCFX_FPS (60000.0 / 1001.0)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#if defined(WANT_32BPP) && defined(FRONTEND_SUPPORTS_RGBA8888)
#define PCFX_SDL_TEXTURE_FORMAT SDL_PIXELFORMAT_ABGR8888
#elif defined(WANT_32BPP)
#define PCFX_SDL_TEXTURE_FORMAT SDL_PIXELFORMAT_ARGB8888
#else
#define PCFX_SDL_TEXTURE_FORMAT SDL_PIXELFORMAT_RGB565
#endif

#define HK_MENU        SDL_SCANCODE_F1
#define HK_FULLSCREEN  SDL_SCANCODE_F11
#define HK_SAVE_STATE  SDL_SCANCODE_F5
#define HK_LOAD_STATE  SDL_SCANCODE_F7
#define HK_PREV_SLOT   SDL_SCANCODE_F6
#define HK_NEXT_SLOT   SDL_SCANCODE_F8
#define HK_SCREENSHOT  SDL_SCANCODE_F9
#define HK_3D_STATUS   SDL_SCANCODE_F10
#define HK_SWAP_DISC   SDL_SCANCODE_F12

enum MenuTab
{
    MENU_TAB_MEDIA = 0,
    MENU_TAB_SYSTEM,
    MENU_TAB_VIDEO,
    MENU_TAB_STATES,
    MENU_TAB_CONTROLS,
    MENU_TAB_COUNT
};

static const char* const k_menu_tabs[MENU_TAB_COUNT] = {
    "Media", "System", "Video", "States", "Controls"
};

struct Binding
{
    const char* name;
    uint16_t bit;
    SDL_Scancode scancode;
    SDL_GamepadButton gp_button;
};

static const struct Binding k_bindings[] = {
    { "Up",       PCFX_PAD_UP,     SDL_SCANCODE_UP,     SDL_GAMEPAD_BUTTON_DPAD_UP },
    { "Down",     PCFX_PAD_DOWN,   SDL_SCANCODE_DOWN,   SDL_GAMEPAD_BUTTON_DPAD_DOWN },
    { "Left",     PCFX_PAD_LEFT,   SDL_SCANCODE_LEFT,   SDL_GAMEPAD_BUTTON_DPAD_LEFT },
    { "Right",    PCFX_PAD_RIGHT,  SDL_SCANCODE_RIGHT,  SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
    { "I/A",      PCFX_PAD_A,      SDL_SCANCODE_Z,      SDL_GAMEPAD_BUTTON_SOUTH },
    { "II/B",     PCFX_PAD_B,      SDL_SCANCODE_X,      SDL_GAMEPAD_BUTTON_EAST },
    { "III/C",    PCFX_PAD_C,      SDL_SCANCODE_C,      SDL_GAMEPAD_BUTTON_WEST },
    { "IV/X",     PCFX_PAD_X,      SDL_SCANCODE_A,      SDL_GAMEPAD_BUTTON_NORTH },
    { "V/Y",      PCFX_PAD_Y,      SDL_SCANCODE_S,      SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
    { "VI/Z",     PCFX_PAD_Z,      SDL_SCANCODE_D,      SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
    { "Start",    PCFX_PAD_START,  SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_START },
    { "Select",   PCFX_PAD_SELECT, SDL_SCANCODE_RSHIFT, SDL_GAMEPAD_BUTTON_BACK }
};

static const SDL_Scancode k_p2_default_scancodes[] = {
    SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L,
    SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3,
    SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6,
    SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_7
};

enum
{
    PCFX_UI_MODE_PCFX = 0,
    PCFX_UI_MODE_FXGA = 1,
    PCFX_UI_MODE_AUTO = 2
};

static const char* system_mode_name(int mode)
{
    switch(mode)
    {
        case PCFX_UI_MODE_FXGA: return "PC-FXGA";
        case PCFX_UI_MODE_AUTO: return "Auto";
        default: return "PC-FX";
    }
}

static int next_system_mode(int mode)
{
    return mode == PCFX_UI_MODE_AUTO ? PCFX_UI_MODE_PCFX : mode + 1;
}

struct App
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* game_tex;
    SDL_AudioStream* audio_stream;
    SDL_Gamepad* gamepad;
    PCFX_Headless* emu;

    char game_path[PATH_MAX];
    char game_id[128];
    char bios_dir[PATH_MAX];
    char save_dir[PATH_MAX];
    char state_dir[PATH_MAX];

    bool running;
    bool paused;
    bool fullscreen;
    bool bilinear;
    bool scanlines;
    bool enable_3d_hardware;
    int system_mode;
    int aspect; /* 0=4:3, 1=native, 2=stretch */
    int state_slot;
    int controller_type; /* 0=gamepad, 1=mouse */
    SDL_Scancode keymap[2][ARRAY_SIZE(k_bindings)];
    int remap_player;
    int remap_button;
    int32_t mouse_dx;
    int32_t mouse_dy;
    uint16_t mouse_buttons;
    bool mouse_captured;
    int display_x;
    int display_y;
    int display_w;
    int display_h;
    double next_frame_ms;
    uint64_t message_until;
    char message[512];
    bool swap_dialog_open;
    bool load_dialog_open;
    bool menu_visible;
    int menu_tab;
    int menu_selection[MENU_TAB_COUNT];
    int menu_hover_tab;
    int menu_hover_item;
    bool fast_video;
    bool pending_load;
    volatile bool pending_swap;
    char pending_load_path[PATH_MAX];
    char pending_swap_path[PATH_MAX];
};

static uint64_t now_ms(void)
{
    return SDL_GetTicks();
}

static void copy_str(char* dst, size_t dst_size, const char* src)
{
    if(dst_size == 0)
        return;
    if(!src)
        src = "";
    snprintf(dst, dst_size, "%s", src);
}


static bool dir_exists(const char* p);

static bool has_ext_ci(const char* path, const char* ext)
{
    size_t plen;
    size_t elen;
    if(!path || !ext)
        return false;
    plen = strlen(path);
    elen = strlen(ext);
    return plen >= elen && !strcasecmp(path + plen - elen, ext);
}

static bool is_huexe_path(const char* path)
{
    return has_ext_ci(path, ".ex") || has_ext_ci(path, ".exe");
}

static bool is_zip_path(const char* path)
{
    return has_ext_ci(path, ".zip");
}

static bool is_supported_boot_media(const char* path)
{
    return is_huexe_path(path) || has_ext_ci(path, ".chd") || has_ext_ci(path, ".cue") ||
           has_ext_ci(path, ".ccd") || has_ext_ci(path, ".toc") || has_ext_ci(path, ".m3u") ||
           has_ext_ci(path, ".bin") || has_ext_ci(path, ".iso") || has_ext_ci(path, ".img") ||
           has_ext_ci(path, ".sub") || has_ext_ci(path, ".wav") || has_ext_ci(path, ".flac") ||
           has_ext_ci(path, ".ogg") || has_ext_ci(path, ".mp3") || has_ext_ci(path, ".aiff") || has_ext_ci(path, ".aif");
}

static int media_boot_priority(const char* path)
{
    if(has_ext_ci(path, ".cue")) return 10;
    if(has_ext_ci(path, ".m3u")) return 20;
    if(has_ext_ci(path, ".toc")) return 30;
    if(has_ext_ci(path, ".ccd")) return 40;
    if(has_ext_ci(path, ".chd")) return 50;
    if(is_huexe_path(path)) return 60;
    if(has_ext_ci(path, ".iso") || has_ext_ci(path, ".bin") || has_ext_ci(path, ".img")) return 70;
    return 1000;
}

static void sanitize_path_component(char* s)
{
    if(!s)
        return;
    for(size_t i = 0; s[i]; i++)
    {
        unsigned char c = (unsigned char)s[i];
        if(c == '\\') s[i] = '/';
        else if(c < 32 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') s[i] = '_';
    }
}

static bool safe_relative_zip_path(char* s)
{
    if(!s || !s[0])
        return false;
    sanitize_path_component(s);
    while(s[0] == '/')
        memmove(s, s + 1, strlen(s));
    if(!s[0] || strstr(s, "../") || strstr(s, "/../") || !strcmp(s, ".."))
        return false;
    return true;
}

static bool make_parent_dirs(const char* path)
{
    char tmp[PATH_MAX];
    size_t len;
    copy_str(tmp, sizeof(tmp), path);
    len = strlen(tmp);
    for(size_t i = 1; i < len; i++)
    {
        if(tmp[i] == '/' || tmp[i] == '\\')
        {
            char old = tmp[i];
            tmp[i] = '\0';
            if(tmp[0] && !dir_exists(tmp) && mkdir(tmp, 0775) != 0 && !dir_exists(tmp))
                return false;
            tmp[i] = old;
        }
    }
    return true;
}

static void set_message(struct App* app, const char* fmt, ...)
{
    va_list ap;
    if(!app)
        return;
    va_start(ap, fmt);
    vsnprintf(app->message, sizeof(app->message), fmt, ap);
    va_end(ap);
    app->message_until = now_ms() + 2500;
    if(app->window)
    {
        char title[768];
        snprintf(title, sizeof(title), "PC-FX / PC-FXGA - %s", app->message);
        SDL_SetWindowTitle(app->window, title);
    }
}

static bool file_exists(const char* p)
{
    struct stat st;
    return p && p[0] && stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static bool dir_exists(const char* p)
{
    struct stat st;
    return p && p[0] && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool make_dir(const char* p)
{
    if(!p || !p[0] || dir_exists(p))
        return true;
    return mkdir(p, 0775) == 0 || dir_exists(p);
}

static void path_join(char* out, size_t out_size, const char* a, const char* b)
{
    size_t alen;
    if(!a || !a[0])
    {
        copy_str(out, out_size, b);
        return;
    }
    alen = strlen(a);
    if(a[alen - 1] == '/' || a[alen - 1] == '\\')
        snprintf(out, out_size, "%s%s", a, b ? b : "");
    else
        snprintf(out, out_size, "%s/%s", a, b ? b : "");
}

static bool pcfx_bios_exists_in_dir(const char* dir)
{
    static const char* names[] = {
        "pcfx.rom", "pcfxbios.bin", "pcfxv101.bin", "pcfx_bios.bin",
        "pcfxga.rom", "pcfxga.bin", "PCFX.ROM", "PCFXBIOS.BIN",
        "PCFXV101.BIN", "PCFXGA.ROM", "PCFXGA.BIN"
    };
    char path[PATH_MAX];
    size_t i;

    if(file_exists(dir))
        return true;
    for(i = 0; i < ARRAY_SIZE(names); i++)
    {
        path_join(path, sizeof(path), dir, names[i]);
        if(file_exists(path))
            return true;
    }
    return false;
}

static void home_pcfxemu_dir(char* out, size_t out_size)
{
    const char* home = getenv("HOME");
    if(!home || !home[0])
        copy_str(out, out_size, ".pcfxemu");
    else
        path_join(out, out_size, home, ".pcfxemu");
}

static void parent_dir_of(char* out, size_t out_size, const char* p)
{
    const char* slash;
    const char* backslash;
    const char* end;
    size_t len;

    if(!p || !p[0])
    {
        copy_str(out, out_size, ".");
        return;
    }

    slash = strrchr(p, '/');
    backslash = strrchr(p, '\\');
    end = slash > backslash ? slash : backslash;
    if(!end)
    {
        copy_str(out, out_size, ".");
        return;
    }
    if(end == p)
    {
        copy_str(out, out_size, "/");
        return;
    }
    len = (size_t)(end - p);
    if(len >= out_size)
        len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

static void basename_noext(char* out, size_t out_size, const char* p)
{
    const char* slash;
    const char* backslash;
    const char* base;
    const char* dot;
    size_t len;
    size_t i;

    if(!p || !p[0])
    {
        copy_str(out, out_size, "pcfx");
        return;
    }

    slash = strrchr(p, '/');
    backslash = strrchr(p, '\\');
    base = slash > backslash ? slash : backslash;
    base = base ? base + 1 : p;
    dot = strrchr(base, '.');
    len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    if(len == 0)
        len = 4;
    if(len >= out_size)
        len = out_size - 1;
    memcpy(out, len == 4 && (!base[0] || base[0] == '.') ? "pcfx" : base, len);
    out[len] = '\0';
    if(!out[0])
        copy_str(out, out_size, "pcfx");
    for(i = 0; out[i]; i++)
    {
        unsigned char c = (unsigned char)out[i];
        if(!(isalnum(c) || c == '_' || c == '-' || c == '.'))
            out[i] = '_';
    }
}


static uint16_t rd16le(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32le(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_file_bytes(const char* path, uint8_t** out, size_t* out_size)
{
    FILE* fp;
    long size;
    uint8_t* data;
    if(out) *out = NULL;
    if(out_size) *out_size = 0;
    if(!path || !out || !out_size)
        return false;
    fp = fopen(path, "rb");
    if(!fp)
        return false;
    if(fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    size = ftell(fp);
    if(size < 0) { fclose(fp); return false; }
    if(fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return false; }
    data = (uint8_t*)malloc((size_t)size ? (size_t)size : 1);
    if(!data) { fclose(fp); return false; }
    if(size && fread(data, 1, (size_t)size, fp) != (size_t)size)
    {
        fclose(fp);
        free(data);
        return false;
    }
    fclose(fp);
    *out = data;
    *out_size = (size_t)size;
    return true;
}

static bool write_file_bytes(const char* path, const uint8_t* data, size_t size)
{
    FILE* fp;
    if(!path || !data)
        return false;
    if(!make_parent_dirs(path))
        return false;
    fp = fopen(path, "wb");
    if(!fp)
        return false;
    if(size && fwrite(data, 1, size, fp) != size)
    {
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}

static bool inflate_zip_deflate(const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_size)
{
    z_stream zs;
    int zret;
    memset(&zs, 0, sizeof(zs));
    zs.next_in = (Bytef*)src;
    zs.avail_in = (uInt)src_size;
    zs.next_out = dst;
    zs.avail_out = (uInt)dst_size;
    if(inflateInit2(&zs, -MAX_WBITS) != Z_OK)
        return false;
    zret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return zret == Z_STREAM_END && zs.total_out == dst_size;
}

static bool zip_extract_supported_media(struct App* app, const char* zip_path, char* boot_out, size_t boot_out_size, bool disc_only)
{
    uint8_t* zip = NULL;
    size_t zip_size = 0;
    size_t eocd = (size_t)-1;
    uint16_t entries;
    uint32_t cd_size;
    uint32_t cd_off;
    char zip_id[192];
    char cache_root[PATH_MAX];
    char best_boot[PATH_MAX];
    int best_pri = 10000;

    if(!read_file_bytes(zip_path, &zip, &zip_size))
        return false;
    if(zip_size < 22)
        goto fail;

    for(size_t p = zip_size - 22; p + 4 <= zip_size && p >= (zip_size > 0x10016 ? zip_size - 0x10016 : 0); p--)
    {
        if(rd32le(zip + p) == 0x06054b50u)
        {
            eocd = p;
            break;
        }
        if(p == 0)
            break;
    }
    if(eocd == (size_t)-1)
        goto fail;

    entries = rd16le(zip + eocd + 10);
    cd_size = rd32le(zip + eocd + 12);
    cd_off = rd32le(zip + eocd + 16);
    if((uint64_t)cd_off + cd_size > zip_size)
        goto fail;

    basename_noext(zip_id, sizeof(zip_id), zip_path);
    snprintf(cache_root, sizeof(cache_root), "%s/zipmedia_%s", app->save_dir, zip_id);
    make_dir(cache_root);
    best_boot[0] = '\0';

    size_t pos = cd_off;
    for(uint16_t i = 0; i < entries; i++)
    {
        if(pos + 46 > zip_size || rd32le(zip + pos) != 0x02014b50u)
            goto fail;
        uint16_t flags = rd16le(zip + pos + 8);
        uint16_t method = rd16le(zip + pos + 10);
        uint32_t comp_size = rd32le(zip + pos + 20);
        uint32_t uncomp_size = rd32le(zip + pos + 24);
        uint16_t name_len = rd16le(zip + pos + 28);
        uint16_t extra_len = rd16le(zip + pos + 30);
        uint16_t comment_len = rd16le(zip + pos + 32);
        uint32_t local_off = rd32le(zip + pos + 42);
        if(pos + 46u + name_len + extra_len + comment_len > zip_size)
            goto fail;

        char rel[PATH_MAX];
        size_t nlen = name_len < sizeof(rel) - 1 ? name_len : sizeof(rel) - 1;
        memcpy(rel, zip + pos + 46, nlen);
        rel[nlen] = '\0';
        pos += 46u + name_len + extra_len + comment_len;

        if(flags & 0x0001) /* encrypted */
            continue;
        if(!safe_relative_zip_path(rel))
            continue;
        if(rel[strlen(rel) - 1] == '/')
            continue;
        /* Extract sidecars as well as boot media.  HuEXE/GMAKER programs can
           load .AIC/.ACD/.AID files through PIOLIB after startup; filtering
           ZIPs down to executable/disc members leaves those programs with
           missing graphics or palettes.  Only supported boot media participate
           in boot-file selection below. */
        if(local_off + 30 > zip_size || rd32le(zip + local_off) != 0x04034b50u)
            goto fail;
        uint16_t lname = rd16le(zip + local_off + 26);
        uint16_t lextra = rd16le(zip + local_off + 28);
        size_t data_off = (size_t)local_off + 30u + lname + lextra;
        if(data_off + comp_size > zip_size)
            goto fail;

        uint8_t* out = (uint8_t*)malloc(uncomp_size ? uncomp_size : 1);
        if(!out)
            goto fail;
        bool ok = false;
        if(method == 0)
        {
            if(comp_size == uncomp_size)
            {
                memcpy(out, zip + data_off, uncomp_size);
                ok = true;
            }
        }
        else if(method == 8)
            ok = inflate_zip_deflate(zip + data_off, comp_size, out, uncomp_size);
        if(!ok)
        {
            free(out);
            goto fail;
        }

        char out_path[PATH_MAX];
        path_join(out_path, sizeof(out_path), cache_root, rel);
        if(!write_file_bytes(out_path, out, uncomp_size))
        {
            free(out);
            goto fail;
        }
        free(out);

        if(is_supported_boot_media(rel) && !(disc_only && is_huexe_path(rel)))
        {
            int pri = media_boot_priority(rel);
            if(pri < best_pri)
            {
                best_pri = pri;
                copy_str(best_boot, sizeof(best_boot), out_path);
            }
        }
    }

    free(zip);
    if(!best_boot[0])
        return false;
    copy_str(boot_out, boot_out_size, best_boot);
    return true;

fail:
    free(zip);
    return false;
}

static bool resolve_frontend_media_path(struct App* app, const char* input_path, char* resolved, size_t resolved_size, bool disc_only)
{
    if(!input_path || !input_path[0])
        return false;
    if(is_zip_path(input_path))
        return zip_extract_supported_media(app, input_path, resolved, resolved_size, disc_only);
    if(disc_only && is_huexe_path(input_path))
        return false;
    copy_str(resolved, resolved_size, input_path);
    return true;
}

static void update_display_rect(struct App* app)
{
    int x = 32;
    int y = 0;
    int w = 256;
    int h = EMU_H;
    pcfx_headless_get_display_rect(app->emu, &x, &y, &w, &h);
    if(x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > EMU_W || y + h > EMU_H)
    {
        x = 0;
        y = 0;
        w = EMU_W;
        h = EMU_H;
    }
    app->display_x = x;
    app->display_y = y;
    app->display_w = w;
    app->display_h = h;
}

static void get_render_size(struct App* app, int* ww, int* wh)
{
    int w = 0;
    int h = 0;
    if(app->renderer)
        SDL_GetCurrentRenderOutputSize(app->renderer, &w, &h);
    if((w <= 0 || h <= 0) && app->window)
        SDL_GetWindowSizeInPixels(app->window, &w, &h);
    if(w <= 0 || h <= 0)
    {
        w = 960;
        h = 720;
    }
    if(ww) *ww = w;
    if(wh) *wh = h;
}

static SDL_FRect compute_game_rect(struct App* app, int ww, int wh)
{
    int src_w = app->display_w > 0 ? app->display_w : 1;
    int src_h = app->display_h > 0 ? app->display_h : 1;
    float target_aspect = 4.0f / 3.0f;
    float dw;
    float dh;
    SDL_FRect rect;

    if(app->aspect == 1)
        target_aspect = (float)src_w / (float)src_h;
    if(app->aspect == 2)
    {
        rect.x = 0.0f;
        rect.y = 0.0f;
        rect.w = (float)ww;
        rect.h = (float)wh;
        return rect;
    }

    dw = (float)ww;
    dh = dw / target_aspect;
    if(dh > (float)wh)
    {
        dh = (float)wh;
        dw = dh * target_aspect;
    }
    if(app->aspect == 1)
    {
        int sx = (int)(dw / src_w);
        int sy = (int)(dh / src_h);
        int s;
        if(sx < 1) sx = 1;
        if(sy < 1) sy = 1;
        s = sx < sy ? sx : sy;
        dw = (float)(src_w * s);
        dh = (float)(src_h * s);
    }
    rect.x = ((float)ww - dw) * 0.5f;
    rect.y = ((float)wh - dh) * 0.5f;
    rect.w = dw;
    rect.h = dh;
    return rect;
}

static void audio_cb(void* userdata, const int16_t* samples, uint32_t frames)
{
    SDL_AudioStream* stream = (SDL_AudioStream*)userdata;
    const int max_queued_bytes = 44100 * 2 * (int)sizeof(int16_t); /* ~1 second cap; normal queue stays far below this. */
    if(!stream || !samples || !frames)
        return;
    /* Do not block the emulation thread inside the audio submit path.  If
       something external stalls playback long enough to exceed the cap, trim
       old queued audio once to regain sync instead of entering a slow loop. */
    if(SDL_GetAudioStreamQueued(stream) > max_queued_bytes)
        SDL_ClearAudioStream(stream);
    SDL_PutAudioStreamData(stream, samples, (int)(frames * 2u * sizeof(int16_t)));
}

static void init_default_keymaps(struct App* app)
{
    if(!app)
        return;
    for(size_t i = 0; i < ARRAY_SIZE(k_bindings); i++)
    {
        app->keymap[0][i] = k_bindings[i].scancode;
        app->keymap[1][i] = i < ARRAY_SIZE(k_p2_default_scancodes) ? k_p2_default_scancodes[i] : SDL_SCANCODE_UNKNOWN;
    }
    app->remap_player = -1;
    app->remap_button = 0;
}

static uint16_t collect_pad_state(struct App* app, const bool* keys, int player)
{
    uint16_t st = 0;
    size_t i;
    if(player < 0 || player > 1)
        return 0;
    for(i = 0; i < ARRAY_SIZE(k_bindings); i++)
    {
        const struct Binding* b = &k_bindings[i];
        SDL_Scancode sc = app->keymap[player][i];
        if(keys && sc != SDL_SCANCODE_UNKNOWN && keys[sc])
            st |= b->bit;
        if(player == 0 && app->gamepad && b->gp_button != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(app->gamepad, b->gp_button))
            st |= b->bit;
    }
    return st;
}

static void state_path(char* out, size_t out_size, const struct App* app, int slot)
{
    char name[192];
    snprintf(name, sizeof(name), "%s_slot%d.mcr", app->game_id, slot);
    path_join(out, out_size, app->state_dir, name);
}

static void save_state_slot(struct App* app, int slot)
{
    char path[PATH_MAX];
    state_path(path, sizeof(path), app, slot);
    if(pcfx_headless_save_state(app->emu, path))
        set_message(app, "Saved state slot %d", slot);
    else
        set_message(app, "Save failed: %s", pcfx_headless_last_error(app->emu));
}

static void load_state_slot(struct App* app, int slot)
{
    char path[PATH_MAX];
    state_path(path, sizeof(path), app, slot);
    if(!file_exists(path))
    {
        set_message(app, "No state in slot %d", slot);
        return;
    }
    if(pcfx_headless_load_state(app->emu, path))
        set_message(app, "Loaded state slot %d", slot);
    else
        set_message(app, "Load failed: %s", pcfx_headless_last_error(app->emu));
}

static void save_screenshot(struct App* app)
{
    char name[192];
    char path[PATH_MAX];
    snprintf(name, sizeof(name), "%s_shot_%llu.ppm", app->game_id, (unsigned long long)pcfx_headless_frame_count(app->emu));
    path_join(path, sizeof(path), app->save_dir, name);
    if(pcfx_headless_save_screenshot_ppm(app->emu, path))
        set_message(app, "Screenshot saved: %s", name);
    else
        set_message(app, "Screenshot failed: %s", pcfx_headless_last_error(app->emu));
}

static const char* aspect_name(const struct App* app);
static void render_menu(struct App* app, int ww, int wh);
static int menu_item_count(int tab);
static void activate_menu_item(struct App* app);
static void clamp_menu_selection(struct App* app);
static void release_mouse_capture(struct App* app);
static void apply_controller_type(struct App* app, int type);

static void render_game(struct App* app)
{
    int ww = 0;
    int wh = 0;
    int w = 0;
    int h = 0;
    int pitch = 0;
    const void* pix;
    int pixel_format = 0;
    int bytes_per_pixel = 0;
    SDL_FRect src;
    SDL_FRect dst;
    int y;

    get_render_size(app, &ww, &wh);
    pix = pcfx_headless_get_pixels(app->emu, &w, &h, &pitch, &pixel_format, &bytes_per_pixel);
    (void)pixel_format;
    update_display_rect(app);
    if(pix)
        SDL_UpdateTexture(app->game_tex, NULL, pix, pitch * bytes_per_pixel);

    SDL_SetRenderDrawColor(app->renderer, 8, 11, 18, 255);
    SDL_RenderClear(app->renderer);

    src.x = (float)app->display_x;
    src.y = (float)app->display_y;
    src.w = (float)app->display_w;
    src.h = (float)app->display_h;
    dst = compute_game_rect(app, ww, wh);
    SDL_RenderTexture(app->renderer, app->game_tex, &src, &dst);

    if(app->scanlines)
    {
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 70);
        for(y = 0; y < (int)dst.h; y += 2)
        {
            SDL_FRect line;
            line.x = dst.x;
            line.y = dst.y + (float)y;
            line.w = dst.w;
            line.h = 1.0f;
            SDL_RenderFillRect(app->renderer, &line);
        }
    }

    if(app->menu_visible)
        render_menu(app, ww, wh);
    else if(app->paused)
    {
        SDL_FRect panel;
        panel.x = 24.0f;
        panel.y = 24.0f;
        panel.w = 360.0f;
        panel.h = 72.0f;
        SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 180);
        SDL_RenderFillRect(app->renderer, &panel);
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_RenderDebugText(app->renderer, panel.x + 14.0f, panel.y + 14.0f, "Paused");
        SDL_RenderDebugText(app->renderer, panel.x + 14.0f, panel.y + 34.0f, "Esc/F1 opens menu");
    }

    if(app->message[0] && now_ms() > app->message_until)
    {
        app->message[0] = '\0';
        SDL_SetWindowTitle(app->window, "PC-FX / PC-FXGA");
    }

    SDL_RenderPresent(app->renderer);
}

static const SDL_DialogFileFilter k_load_filters[] = {
    { "PC-FX / PC-FXGA media", "cue;ccd;toc;m3u;chd;iso;bin;img;ex;exe;zip" },
    { "CD images", "cue;ccd;toc;m3u;chd;iso;bin;img" },
    { "PC-FXGA homebrew", "ex;exe" },
    { "ZIP archives", "zip" },
    { "All files", "*" }
};

static const SDL_DialogFileFilter k_disc_filters[] = {
    { "PC-FX disc images", "cue;ccd;toc;m3u;chd;iso;bin;img;zip" },
    { "CHD images", "chd" },
    { "CUE/BIN images", "cue;bin;img;iso" },
    { "ZIP archives", "zip" },
    { "All files", "*" }
};

static void load_game_dialog_callback(void* userdata, const char* const* filelist, int filter)
{
    (void)filter;
    struct App* app = (struct App*)userdata;
    if(!app)
        return;
    app->load_dialog_open = false;
    if(!filelist)
    {
        copy_str(app->pending_load_path, sizeof(app->pending_load_path), "");
        app->pending_load = false;
        return;
    }
    if(filelist[0] && filelist[0][0])
    {
        copy_str(app->pending_load_path, sizeof(app->pending_load_path), filelist[0]);
        app->pending_load = true;
    }
}

static void swap_disc_dialog_callback(void* userdata, const char* const* filelist, int filter)
{
    (void)filter;
    struct App* app = (struct App*)userdata;
    if(!app)
        return;
    app->swap_dialog_open = false;
    if(!filelist)
    {
        copy_str(app->pending_swap_path, sizeof(app->pending_swap_path), "");
        app->pending_swap = false;
        return;
    }
    if(filelist[0] && filelist[0][0])
    {
        copy_str(app->pending_swap_path, sizeof(app->pending_swap_path), filelist[0]);
        app->pending_swap = true;
    }
}

static void open_load_game_dialog(struct App* app)
{
    char start_dir[PATH_MAX];
    if(!app || app->load_dialog_open)
        return;
    parent_dir_of(start_dir, sizeof(start_dir), app->game_path[0] ? app->game_path : ".");
    app->load_dialog_open = true;
    app->paused = true;
    app->menu_visible = true;
    SDL_ShowOpenFileDialog(load_game_dialog_callback, app, app->window, k_load_filters, (int)ARRAY_SIZE(k_load_filters), start_dir, false);
    set_message(app, "Choose PC-FX/PC-FXGA media...");
}

static void open_swap_disc_dialog(struct App* app)
{
    char start_dir[PATH_MAX];
    if(!app || app->swap_dialog_open)
        return;
    parent_dir_of(start_dir, sizeof(start_dir), app->game_path[0] ? app->game_path : ".");
    app->swap_dialog_open = true;
    app->paused = true;
    app->menu_visible = true;
    SDL_ShowOpenFileDialog(swap_disc_dialog_callback, app, app->window, k_disc_filters, (int)ARRAY_SIZE(k_disc_filters), start_dir, false);
    set_message(app, "Choose replacement disc image...");
}

static bool recreate_emulator_for_path(struct App* app, const char* selected_path)
{
    char resolved_path[PATH_MAX];
    PCFX_HeadlessConfig cfg;

    if(!app || !selected_path || !selected_path[0])
        return false;
    if(!resolve_frontend_media_path(app, selected_path, resolved_path, sizeof(resolved_path), false))
    {
        set_message(app, "Load failed: unsupported or unreadable media archive");
        return false;
    }

    if(app->audio_stream)
        SDL_ClearAudioStream(app->audio_stream);
    if(app->emu)
    {
        pcfx_headless_destroy(app->emu);
        app->emu = NULL;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.bios_dir = app->bios_dir;
    cfg.save_dir = app->save_dir;
    cfg.sound_rate = 44100;
    cfg.fast_video = app->fast_video ? 1 : 0;
    cfg.disable_3d_hardware = app->enable_3d_hardware ? 0 : 1;
    cfg.prefer_fxga_bios = app->system_mode;
    app->emu = pcfx_headless_create(&cfg);
    if(!app->emu)
    {
        set_message(app, "Failed to recreate emulator core");
        return false;
    }
    pcfx_headless_set_audio_callback(app->emu, audio_cb, app->audio_stream);
    pcfx_headless_set_controller_type(app->emu, (uint8_t)app->controller_type);
    release_mouse_capture(app);
    if(!pcfx_headless_load_cd(app->emu, resolved_path))
    {
        set_message(app, "Load failed: %s", pcfx_headless_last_error(app->emu));
        return false;
    }

    copy_str(app->game_path, sizeof(app->game_path), selected_path);
    basename_noext(app->game_id, sizeof(app->game_id), resolved_path);
    app->display_x = 32;
    app->display_y = 0;
    app->display_w = 256;
    app->display_h = EMU_H;
    app->next_frame_ms = (double)now_ms() + 1000.0 / PCFX_FPS;
    app->paused = false;
    app->menu_visible = false;
    set_message(app, "Loaded %s", app->game_id);
    return true;
}

static void perform_pending_load_game(struct App* app)
{
    if(!app || !app->pending_load)
        return;
    app->pending_load = false;
    if(!app->pending_load_path[0])
        return;
    recreate_emulator_for_path(app, app->pending_load_path);
}

static void perform_pending_swap_disc(struct App* app)
{
    char resolved_path[PATH_MAX];
    if(!app || !app->pending_swap)
        return;
    app->pending_swap = false;
    if(!app->pending_swap_path[0])
        return;
    if(!resolve_frontend_media_path(app, app->pending_swap_path, resolved_path, sizeof(resolved_path), true))
    {
        set_message(app, "Disc swap failed: choose a disc image, not HuEXE boot media");
        return;
    }
    if(!pcfx_headless_swap_disc(app->emu, resolved_path))
    {
        set_message(app, "Disc swap failed: %s", pcfx_headless_last_error(app->emu));
        return;
    }
    copy_str(app->game_path, sizeof(app->game_path), app->pending_swap_path);
    basename_noext(app->game_id, sizeof(app->game_id), resolved_path);
    if(app->audio_stream)
        SDL_ClearAudioStream(app->audio_stream);
    app->next_frame_ms = (double)now_ms() + 1000.0 / PCFX_FPS;
    app->paused = false;
    app->menu_visible = false;
    set_message(app, "Disc swapped: %s", app->game_id);
}

static const char* aspect_name(const struct App* app)
{
    return app->aspect == 1 ? "Native pixels" : (app->aspect == 2 ? "Stretch" : "4:3 display");
}

static const char* controller_type_name(const struct App* app)
{
    return app->controller_type == 1 ? "Mouse" : "Gamepad";
}

static void release_mouse_capture(struct App* app)
{
    if(!app)
        return;
    if(app->mouse_captured && app->window)
        SDL_SetWindowRelativeMouseMode(app->window, false);
    app->mouse_captured = false;
    app->mouse_dx = 0;
    app->mouse_dy = 0;
    app->mouse_buttons = 0;
}

static void apply_controller_type(struct App* app, int type)
{
    if(!app)
        return;
    app->controller_type = (type == 1) ? 1 : 0;
    if(app->controller_type != 1)
        release_mouse_capture(app);
    if(app->emu)
        pcfx_headless_set_controller_type(app->emu, (uint8_t)app->controller_type);
    if(app->controller_type == 1)
        set_message(app, "Controller: Mouse. Click the game window to capture mouse input.");
    else
        set_message(app, "Controller: Gamepad");
}

static int menu_item_count(int tab)
{
    switch(tab)
    {
        case MENU_TAB_MEDIA: return 5;
        case MENU_TAB_SYSTEM: return 6;
        case MENU_TAB_VIDEO: return 4;
        case MENU_TAB_STATES: return 5;
        case MENU_TAB_CONTROLS: return 10;
        default: return 0;
    }
}

static void clamp_menu_selection(struct App* app)
{
    if(!app)
        return;
    if(app->menu_tab < 0)
        app->menu_tab = 0;
    if(app->menu_tab >= MENU_TAB_COUNT)
        app->menu_tab = MENU_TAB_COUNT - 1;
    for(int t = 0; t < MENU_TAB_COUNT; t++)
    {
        int count = menu_item_count(t);
        if(app->menu_selection[t] < 0)
            app->menu_selection[t] = 0;
        if(count <= 0)
            app->menu_selection[t] = 0;
        else if(app->menu_selection[t] >= count)
            app->menu_selection[t] = count - 1;
    }
}

static const char* key_name(SDL_Scancode sc)
{
    const char* n = SDL_GetScancodeName(sc);
    return (n && n[0]) ? n : "Unmapped";
}

static void menu_item_label(const struct App* app, int tab, int index, char* out, size_t out_size)
{
    const char* label = "";
    if(!out || !out_size)
        return;

    switch(tab)
    {
        case MENU_TAB_MEDIA:
            switch(index)
            {
                case 0: label = "Resume"; break;
                case 1: label = "Load game / homebrew..."; break;
                case 2: label = "Swap disc..."; break;
                case 3: label = "Save screenshot"; break;
                case 4: label = "Quit emulator"; break;
            }
            break;
        case MENU_TAB_SYSTEM:
            switch(index)
            {
                case 0: snprintf(out, out_size, "Hardware mode: %s", system_mode_name(app->system_mode)); return;
                case 1: snprintf(out, out_size, "HuC6273 3D chip: %s", app->enable_3d_hardware ? "enabled" : "disabled"); return;
                case 2: snprintf(out, out_size, "Fast video core: %s", app->fast_video ? "on" : "off"); return;
                case 3: label = "Soft reset current system"; break;
                case 4: label = "Reload current media"; break;
                case 5: snprintf(out, out_size, "BIOS path: %.86s", app->bios_dir[0] ? app->bios_dir : "."); return;
            }
            break;
        case MENU_TAB_VIDEO:
            switch(index)
            {
                case 0: snprintf(out, out_size, "Aspect: %s", aspect_name(app)); return;
                case 1: snprintf(out, out_size, "Scaling filter: %s", app->bilinear ? "linear" : "nearest"); return;
                case 2: snprintf(out, out_size, "Scanlines: %s", app->scanlines ? "on" : "off"); return;
                case 3: snprintf(out, out_size, "Fullscreen: %s", app->fullscreen ? "on" : "off"); return;
            }
            break;
        case MENU_TAB_STATES:
            switch(index)
            {
                case 0: label = "Save state"; break;
                case 1: label = "Load state"; break;
                case 2: snprintf(out, out_size, "State slot: %d", app->state_slot); return;
                case 3: label = "Previous slot"; break;
                case 4: label = "Next slot"; break;
            }
            break;
        case MENU_TAB_CONTROLS:
            switch(index)
            {
                case 0: snprintf(out, out_size, "Controller type: %s", controller_type_name(app)); return;
                case 1: snprintf(out, out_size, "P1 remap target: %s = %s", k_bindings[app->remap_button].name, key_name(app->keymap[0][app->remap_button])); return;
                case 2: label = "Set P1 selected key..."; break;
                case 3: label = "Next P1 key"; break;
                case 4: snprintf(out, out_size, "P2 remap target: %s = %s", k_bindings[app->remap_button].name, key_name(app->keymap[1][app->remap_button])); return;
                case 5: label = "Set P2 selected key..."; break;
                case 6: label = "Next P2 key"; break;
                case 7: label = "Mouse mode: click game window to capture, Esc releases"; break;
                case 8: label = "Menu mouse: click tabs and rows"; break;
                case 9: label = "Hotkeys: F5 save, F7 load, F11 fullscreen, F12 swap"; break;
            }
            break;
    }
    snprintf(out, out_size, "%s", label);
}

static void reload_current_media_keep_menu(struct App* app)
{
    bool keep_menu;
    int old_tab;
    int old_selection[MENU_TAB_COUNT];
    if(!app || !app->game_path[0])
        return;
    keep_menu = app->menu_visible;
    old_tab = app->menu_tab;
    memcpy(old_selection, app->menu_selection, sizeof(old_selection));
    if(recreate_emulator_for_path(app, app->game_path) && keep_menu)
    {
        app->menu_visible = true;
        app->paused = true;
        app->menu_tab = old_tab;
        memcpy(app->menu_selection, old_selection, sizeof(app->menu_selection));
        clamp_menu_selection(app);
    }
}

static void toggle_menu(struct App* app)
{
    if(!app)
        return;
    app->menu_visible = !app->menu_visible;
    if(app->menu_visible)
        release_mouse_capture(app);
    app->paused = app->menu_visible;
    if(app->menu_visible)
        clamp_menu_selection(app);
    set_message(app, app->menu_visible ? "Menu: mouse, arrows/Enter, Esc or F1 resumes" : "Running");
}

static void activate_menu_item(struct App* app)
{
    int idx;
    if(!app)
        return;
    clamp_menu_selection(app);
    idx = app->menu_selection[app->menu_tab];
    switch(app->menu_tab)
    {
        case MENU_TAB_MEDIA:
            switch(idx)
            {
                case 0:
                    app->menu_visible = false;
                    app->paused = false;
                    set_message(app, "Running");
                    break;
                case 1:
                    open_load_game_dialog(app);
                    break;
                case 2:
                    open_swap_disc_dialog(app);
                    break;
                case 3:
                    save_screenshot(app);
                    break;
                case 4:
                    app->running = false;
                    break;
            }
            break;
        case MENU_TAB_SYSTEM:
            switch(idx)
            {
                case 0:
                    app->system_mode = next_system_mode(app->system_mode);
                    set_message(app, "Hardware mode: %s", system_mode_name(app->system_mode));
                    reload_current_media_keep_menu(app);
                    break;
                case 1:
                    app->enable_3d_hardware = !app->enable_3d_hardware;
                    set_message(app, "HuC6273 3D chip: %s", app->enable_3d_hardware ? "enabled" : "disabled");
                    reload_current_media_keep_menu(app);
                    break;
                case 2:
                    app->fast_video = !app->fast_video;
                    set_message(app, "Fast video core: %s", app->fast_video ? "on" : "off");
                    reload_current_media_keep_menu(app);
                    break;
                case 3:
                    if(pcfx_headless_soft_reset(app->emu)) set_message(app, "Soft reset");
                    else set_message(app, "Soft reset failed: %s", pcfx_headless_last_error(app->emu));
                    break;
                case 4:
                    reload_current_media_keep_menu(app);
                    break;
                case 5:
                    set_message(app, "BIOS path: %s", app->bios_dir[0] ? app->bios_dir : ".");
                    break;
            }
            break;
        case MENU_TAB_VIDEO:
            switch(idx)
            {
                case 0:
                    app->aspect = (app->aspect + 1) % 3;
                    set_message(app, "Aspect: %s", aspect_name(app));
                    break;
                case 1:
                    app->bilinear = !app->bilinear;
                    if(app->game_tex)
                        SDL_SetTextureScaleMode(app->game_tex, app->bilinear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
                    set_message(app, "Scaling filter: %s", app->bilinear ? "linear" : "nearest");
                    break;
                case 2:
                    app->scanlines = !app->scanlines;
                    set_message(app, "Scanlines: %s", app->scanlines ? "on" : "off");
                    break;
                case 3:
                    app->fullscreen = !app->fullscreen;
                    SDL_SetWindowFullscreen(app->window, app->fullscreen);
                    set_message(app, app->fullscreen ? "Fullscreen" : "Windowed");
                    break;
            }
            break;
        case MENU_TAB_STATES:
            switch(idx)
            {
                case 0:
                    save_state_slot(app, app->state_slot);
                    break;
                case 1:
                    load_state_slot(app, app->state_slot);
                    break;
                case 2:
                case 4:
                    app->state_slot = (app->state_slot + 1) % 10;
                    set_message(app, "State slot %d", app->state_slot);
                    break;
                case 3:
                    app->state_slot = (app->state_slot + 9) % 10;
                    set_message(app, "State slot %d", app->state_slot);
                    break;
            }
            break;
        case MENU_TAB_CONTROLS:
            if(idx == 0)
                apply_controller_type(app, app->controller_type ? 0 : 1);
            else if(idx == 2 || idx == 5)
            {
                app->remap_player = (idx == 5) ? 1 : 0;
                set_message(app, "Press a key for P%d %s", app->remap_player + 1, k_bindings[app->remap_button].name);
            }
            else if(idx == 3 || idx == 6)
            {
                app->remap_button = (app->remap_button + 1) % (int)ARRAY_SIZE(k_bindings);
                set_message(app, "Remap target: %s", k_bindings[app->remap_button].name);
            }
            else
                set_message(app, "Controls tab is informational");
            break;
    }
}

static void handle_menu_key(struct App* app, SDL_Scancode s)
{
    int count;
    if(!app)
        return;
    clamp_menu_selection(app);
    count = menu_item_count(app->menu_tab);
    if(s == SDL_SCANCODE_ESCAPE || s == HK_MENU)
    {
        app->menu_visible = false;
        app->paused = false;
        set_message(app, "Running");
        return;
    }
    if(s == SDL_SCANCODE_LEFT || s == SDL_SCANCODE_A)
    {
        app->menu_tab = (app->menu_tab + MENU_TAB_COUNT - 1) % MENU_TAB_COUNT;
        clamp_menu_selection(app);
        return;
    }
    if(s == SDL_SCANCODE_RIGHT || s == SDL_SCANCODE_D)
    {
        app->menu_tab = (app->menu_tab + 1) % MENU_TAB_COUNT;
        clamp_menu_selection(app);
        return;
    }
    if(s == SDL_SCANCODE_UP || s == SDL_SCANCODE_W)
    {
        if(count > 0)
            app->menu_selection[app->menu_tab] = (app->menu_selection[app->menu_tab] + count - 1) % count;
        return;
    }
    if(s == SDL_SCANCODE_DOWN || s == SDL_SCANCODE_S)
    {
        if(count > 0)
            app->menu_selection[app->menu_tab] = (app->menu_selection[app->menu_tab] + 1) % count;
        return;
    }
    if(s == SDL_SCANCODE_RETURN || s == SDL_SCANCODE_KP_ENTER || s == SDL_SCANCODE_SPACE || s == SDL_SCANCODE_Z)
        activate_menu_item(app);
}

static void menu_layout(int ww, int wh, SDL_FRect* panel, SDL_FRect* tab_area, SDL_FRect* content_area)
{
    float pw = (float)(ww - 80);
    float ph = (float)(wh - 80);
    if(pw > 820.0f) pw = 820.0f;
    if(ph > 540.0f) ph = 540.0f;
    if(pw < 600.0f) pw = (float)(ww > 620 ? 600 : ww - 20);
    if(ph < 420.0f) ph = (float)(wh > 440 ? 420 : wh - 20);
    panel->x = ((float)ww - pw) * 0.5f;
    panel->y = ((float)wh - ph) * 0.5f;
    panel->w = pw;
    panel->h = ph;
    tab_area->x = panel->x + 16.0f;
    tab_area->y = panel->y + 58.0f;
    tab_area->w = 168.0f;
    tab_area->h = panel->h - 74.0f;
    content_area->x = tab_area->x + tab_area->w + 16.0f;
    content_area->y = tab_area->y;
    content_area->w = panel->w - tab_area->w - 48.0f;
    content_area->h = tab_area->h;
}

static void menu_hit_test(struct App* app, float mx, float my, int ww, int wh, int* out_tab, int* out_item)
{
    SDL_FRect panel, tabs, content;
    if(out_tab) *out_tab = -1;
    if(out_item) *out_item = -1;
    if(!app)
        return;
    menu_layout(ww, wh, &panel, &tabs, &content);
    for(int t = 0; t < MENU_TAB_COUNT; t++)
    {
        SDL_FRect r = { tabs.x, tabs.y + (float)t * 52.0f, tabs.w, 42.0f };
        if(mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h)
        {
            if(out_tab) *out_tab = t;
            return;
        }
    }
    if(mx >= content.x && mx <= content.x + content.w && my >= content.y && my <= content.y + content.h)
    {
        int count = menu_item_count(app->menu_tab);
        for(int i = 0; i < count; i++)
        {
            SDL_FRect r = { content.x + 14.0f, content.y + 54.0f + (float)i * 44.0f, content.w - 28.0f, 34.0f };
            if(mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h)
            {
                if(out_item) *out_item = i;
                return;
            }
        }
    }
}

static void render_menu(struct App* app, int ww, int wh)
{
    SDL_FRect panel, tabs, content;
    char label[192];
    if(!app || !app->renderer)
        return;
    clamp_menu_selection(app);
    menu_layout(ww, wh, &panel, &tabs, &content);

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 6, 9, 16, 232);
    SDL_RenderFillRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 89, 116, 148, 255);
    SDL_RenderRect(app->renderer, &panel);

    SDL_SetRenderDrawColor(app->renderer, 238, 244, 250, 255);
    SDL_RenderDebugText(app->renderer, panel.x + 18.0f, panel.y + 16.0f, "PC-FX / PC-FXGA");
    SDL_SetRenderDrawColor(app->renderer, 160, 176, 194, 255);
    SDL_RenderDebugText(app->renderer, panel.x + 18.0f, panel.y + 34.0f, "Mouse-first menu  |  Esc/F1 closes  |  Arrows/Enter and gamepad supported");

    for(int t = 0; t < MENU_TAB_COUNT; t++)
    {
        SDL_FRect r = { tabs.x, tabs.y + (float)t * 52.0f, tabs.w, 42.0f };
        bool active = (t == app->menu_tab);
        bool hover = (t == app->menu_hover_tab);
        SDL_SetRenderDrawColor(app->renderer, active ? 34 : (hover ? 24 : 13), active ? 68 : (hover ? 42 : 21), active ? 103 : (hover ? 63 : 32), 238);
        SDL_RenderFillRect(app->renderer, &r);
        SDL_SetRenderDrawColor(app->renderer, active ? 154 : 79, active ? 189 : 111, active ? 225 : 146, 255);
        SDL_RenderRect(app->renderer, &r);
        SDL_SetRenderDrawColor(app->renderer, 235, 241, 247, 255);
        SDL_RenderDebugText(app->renderer, r.x + 14.0f, r.y + 13.0f, k_menu_tabs[t]);
    }

    SDL_SetRenderDrawColor(app->renderer, 15, 22, 32, 238);
    SDL_RenderFillRect(app->renderer, &content);
    SDL_SetRenderDrawColor(app->renderer, 64, 86, 112, 255);
    SDL_RenderRect(app->renderer, &content);

    SDL_SetRenderDrawColor(app->renderer, 238, 244, 250, 255);
    SDL_RenderDebugText(app->renderer, content.x + 16.0f, content.y + 16.0f, k_menu_tabs[app->menu_tab]);
    SDL_SetRenderDrawColor(app->renderer, 137, 153, 172, 255);
    SDL_RenderDebugText(app->renderer, content.x + 16.0f, content.y + 32.0f, "Click a row to activate. Left/right changes tabs from keyboard or controller.");

    int count = menu_item_count(app->menu_tab);
    for(int i = 0; i < count; i++)
    {
        SDL_FRect r = { content.x + 14.0f, content.y + 54.0f + (float)i * 44.0f, content.w - 28.0f, 34.0f };
        bool selected = (i == app->menu_selection[app->menu_tab]);
        bool hover = (i == app->menu_hover_item);
        menu_item_label(app, app->menu_tab, i, label, sizeof(label));
        SDL_SetRenderDrawColor(app->renderer, selected ? 37 : (hover ? 27 : 18), selected ? 74 : (hover ? 48 : 28), selected ? 111 : (hover ? 67 : 39), selected ? 235 : 218);
        SDL_RenderFillRect(app->renderer, &r);
        SDL_SetRenderDrawColor(app->renderer, selected ? 146 : 56, selected ? 181 : 77, selected ? 218 : 98, 255);
        SDL_RenderRect(app->renderer, &r);
        SDL_SetRenderDrawColor(app->renderer, 233, 239, 246, 255);
        SDL_RenderDebugText(app->renderer, r.x + 12.0f, r.y + 10.0f, label);
    }
}

static void handle_key(struct App* app, SDL_Scancode s)
{
    if(app->menu_visible)
    {
        handle_menu_key(app, s);
        return;
    }
    if(s == SDL_SCANCODE_ESCAPE || s == HK_MENU)
    {
        toggle_menu(app);
        return;
    }
    if(s == HK_FULLSCREEN)
    {
        app->fullscreen = !app->fullscreen;
        SDL_SetWindowFullscreen(app->window, app->fullscreen);
        set_message(app, app->fullscreen ? "Fullscreen" : "Windowed");
        return;
    }
    if(s == HK_SAVE_STATE)
    {
        save_state_slot(app, app->state_slot);
        return;
    }
    if(s == HK_LOAD_STATE)
    {
        load_state_slot(app, app->state_slot);
        return;
    }
    if(s == HK_PREV_SLOT)
    {
        app->state_slot = (app->state_slot + 9) % 10;
        set_message(app, "State slot %d", app->state_slot);
        return;
    }
    if(s == HK_NEXT_SLOT)
    {
        app->state_slot = (app->state_slot + 1) % 10;
        set_message(app, "State slot %d", app->state_slot);
        return;
    }
    if(s == HK_SCREENSHOT)
    {
        save_screenshot(app);
        return;
    }
    if(s == HK_3D_STATUS)
    {
        set_message(app, "HuC6273 3D chip: %s (startup option)", app->enable_3d_hardware ? "enabled" : "disabled");
        return;
    }
    if(s == HK_SWAP_DISC)
    {
        open_swap_disc_dialog(app);
        return;
    }
}

static void handle_event(struct App* app, const SDL_Event* e)
{
    if(e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && app->remap_player >= 0)
    {
        if(app->remap_player < 2 && app->remap_button >= 0 && app->remap_button < (int)ARRAY_SIZE(k_bindings))
            app->keymap[app->remap_player][app->remap_button] = e->key.scancode;
        set_message(app, "P%d %s = %s", app->remap_player + 1, k_bindings[app->remap_button].name, key_name(e->key.scancode));
        app->remap_player = -1;
        return;
    }
    if(e->type == SDL_EVENT_QUIT)
    {
        app->running = false;
        return;
    }
    if(e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && e->key.scancode == SDL_SCANCODE_ESCAPE && app->mouse_captured)
    {
        release_mouse_capture(app);
        set_message(app, "Mouse released");
        return;
    }
    if(e->type == SDL_EVENT_GAMEPAD_ADDED && !app->gamepad)
    {
        app->gamepad = SDL_OpenGamepad(e->gdevice.which);
        return;
    }
    if(e->type == SDL_EVENT_GAMEPAD_REMOVED)
    {
        if(app->gamepad && SDL_GetGamepadID(app->gamepad) == e->gdevice.which)
        {
            SDL_CloseGamepad(app->gamepad);
            app->gamepad = NULL;
        }
        return;
    }
    if(e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        SDL_GamepadButton b = (SDL_GamepadButton)e->gbutton.button;
        if(b == SDL_GAMEPAD_BUTTON_GUIDE)
        {
            toggle_menu(app);
            return;
        }
        if(app->menu_visible)
        {
            if(b == SDL_GAMEPAD_BUTTON_DPAD_UP) handle_menu_key(app, SDL_SCANCODE_UP);
            else if(b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) handle_menu_key(app, SDL_SCANCODE_DOWN);
            else if(b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) handle_menu_key(app, SDL_SCANCODE_LEFT);
            else if(b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) handle_menu_key(app, SDL_SCANCODE_RIGHT);
            else if(b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START) activate_menu_item(app);
            else if(b == SDL_GAMEPAD_BUTTON_EAST) handle_menu_key(app, SDL_SCANCODE_ESCAPE);
            return;
        }
    }
    if(!app->menu_visible && !app->paused && app->controller_type == 1 && e->type == SDL_EVENT_MOUSE_MOTION && app->mouse_captured)
    {
        app->mouse_dx += (int32_t)e->motion.xrel;
        app->mouse_dy += (int32_t)e->motion.yrel;
        return;
    }
    if(!app->menu_visible && !app->paused && app->controller_type == 1 && e->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if(!app->mouse_captured)
        {
            SDL_SetWindowRelativeMouseMode(app->window, true);
            app->mouse_captured = true;
            app->mouse_dx = 0;
            app->mouse_dy = 0;
            set_message(app, "Mouse captured. Press Esc to release.");
        }
        if(e->button.button == SDL_BUTTON_LEFT) app->mouse_buttons |= 1;
        else if(e->button.button == SDL_BUTTON_RIGHT) app->mouse_buttons |= 2;
        else if(e->button.button == SDL_BUTTON_MIDDLE) app->mouse_buttons |= 4;
        return;
    }
    if(!app->menu_visible && app->controller_type == 1 && e->type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if(e->button.button == SDL_BUTTON_LEFT) app->mouse_buttons &= (uint16_t)~1u;
        else if(e->button.button == SDL_BUTTON_RIGHT) app->mouse_buttons &= (uint16_t)~2u;
        else if(e->button.button == SDL_BUTTON_MIDDLE) app->mouse_buttons &= (uint16_t)~4u;
        return;
    }
    if(app->menu_visible && e->type == SDL_EVENT_MOUSE_MOTION)
    {
        int ww = 0, wh = 0;
        int tab = -1, item = -1;
        SDL_Event re = *e;
        if(app->renderer)
            SDL_ConvertEventToRenderCoordinates(app->renderer, &re);
        get_render_size(app, &ww, &wh);
        menu_hit_test(app, re.motion.x, re.motion.y, ww, wh, &tab, &item);
        app->menu_hover_tab = tab;
        app->menu_hover_item = item;
        if(item >= 0)
            app->menu_selection[app->menu_tab] = item;
        return;
    }
    if(app->menu_visible && e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT)
    {
        int ww = 0, wh = 0;
        int tab = -1, item = -1;
        SDL_Event re = *e;
        if(app->renderer)
            SDL_ConvertEventToRenderCoordinates(app->renderer, &re);
        get_render_size(app, &ww, &wh);
        menu_hit_test(app, re.button.x, re.button.y, ww, wh, &tab, &item);
        if(tab >= 0)
        {
            app->menu_tab = tab;
            clamp_menu_selection(app);
            return;
        }
        if(item >= 0)
        {
            app->menu_selection[app->menu_tab] = item;
            activate_menu_item(app);
            return;
        }
    }
    if(e->type == SDL_EVENT_DROP_FILE && e->drop.data)
    {
        copy_str(app->pending_swap_path, sizeof(app->pending_swap_path), e->drop.data);
        app->pending_swap = true;
        SDL_free((void*)e->drop.data);
        return;
    }
    if(e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat)
        handle_key(app, e->key.scancode);
}

static void print_usage(const char* argv0)
{
    fprintf(stderr,
            "Usage: %s [--bios-dir DIR|BIOS] [--save-dir DIR] [--fast-video] [--disable-3d-hardware] [--auto] [--pcfx] [--pcfxga] [--fullscreen] [--native-aspect] [--stretch] [--nearest] [--scanlines] [--mouse] game.cue|game.chd|game.zip|homebrew_dir|program.EX\n\n"
            "Keyboard P1: arrows, Z/X/C, A/S/D, Enter, Right Shift. P2: IJKL, numpad 1-8. Hotkeys: Esc/F1 menu, F5 save, F7 load, F6/F8 slot, F9 screenshot, F10 3D status, F11 fullscreen, F12 swap disc.\n",
            argv0);
}

int main(int argc, char** argv)
{
    struct App app;
    bool disable_3d_hardware = false;
    bool bios_dir_explicit = false;
    bool save_dir_explicit = false;
    char home_dir[PATH_MAX];
    int i;
    PCFX_HeadlessConfig cfg;
    SDL_AudioSpec spec;

    memset(&app, 0, sizeof(app));
    init_default_keymaps(&app);
    copy_str(app.bios_dir, sizeof(app.bios_dir), ".");
    app.running = true;
    app.bilinear = true;
    app.enable_3d_hardware = true;
    app.system_mode = PCFX_UI_MODE_AUTO;
    app.menu_hover_tab = -1;
    app.menu_hover_item = -1;
    app.aspect = 0;
    app.display_x = 32;
    app.display_y = 0;
    app.display_w = 256;
    app.display_h = EMU_H;

    for(i = 1; i < argc; i++)
    {
        if(!strcmp(argv[i], "--bios-dir") && i + 1 < argc)
        {
            copy_str(app.bios_dir, sizeof(app.bios_dir), argv[++i]);
            bios_dir_explicit = true;
        }
        else if(!strcmp(argv[i], "--save-dir") && i + 1 < argc)
        {
            copy_str(app.save_dir, sizeof(app.save_dir), argv[++i]);
            save_dir_explicit = true;
        }
        else if(!strcmp(argv[i], "--fast-video"))
            app.fast_video = true;
        else if(!strcmp(argv[i], "--disable-3d-hardware"))
            disable_3d_hardware = true;
        else if(!strcmp(argv[i], "--auto"))
            app.system_mode = PCFX_UI_MODE_AUTO;
        else if(!strcmp(argv[i], "--pcfx"))
            app.system_mode = PCFX_UI_MODE_PCFX;
        else if(!strcmp(argv[i], "--pcfxga"))
            app.system_mode = PCFX_UI_MODE_FXGA;
        else if(!strcmp(argv[i], "--fullscreen"))
            app.fullscreen = true;
        else if(!strcmp(argv[i], "--native-aspect"))
            app.aspect = 1;
        else if(!strcmp(argv[i], "--stretch"))
            app.aspect = 2;
        else if(!strcmp(argv[i], "--nearest"))
            app.bilinear = false;
        else if(!strcmp(argv[i], "--scanlines"))
            app.scanlines = true;
        else if(!strcmp(argv[i], "--mouse"))
            app.controller_type = 1;
        else if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
            print_usage(argv[0]);
            return 0;
        }
        else
            copy_str(app.game_path, sizeof(app.game_path), argv[i]);
    }

    if(!app.game_path[0])
    {
        app.menu_visible = true;
        copy_str(app.game_id, sizeof(app.game_id), app.system_mode == PCFX_UI_MODE_FXGA ? "PC-FXGA BIOS" : app.system_mode == PCFX_UI_MODE_AUTO ? "Auto BIOS" : "PC-FX BIOS");
    }

    home_pcfxemu_dir(home_dir, sizeof(home_dir));
    make_dir(home_dir);
    if(!save_dir_explicit)
        copy_str(app.save_dir, sizeof(app.save_dir), home_dir);
    if(!bios_dir_explicit)
    {
        char game_dir[PATH_MAX];
        game_dir[0] = 0;
        if(app.game_path[0])
            parent_dir_of(game_dir, sizeof(game_dir), app.game_path);
        if(pcfx_bios_exists_in_dir("."))
            copy_str(app.bios_dir, sizeof(app.bios_dir), ".");
        else if(game_dir[0] && pcfx_bios_exists_in_dir(game_dir))
            copy_str(app.bios_dir, sizeof(app.bios_dir), game_dir);
        else if(pcfx_bios_exists_in_dir(home_dir))
            copy_str(app.bios_dir, sizeof(app.bios_dir), home_dir);
        else
            copy_str(app.bios_dir, sizeof(app.bios_dir), ".");
    }

    if(app.game_path[0])
        basename_noext(app.game_id, sizeof(app.game_id), app.game_path);
    make_dir(app.save_dir);
    path_join(app.state_dir, sizeof(app.state_dir), app.save_dir, "states");
    make_dir(app.state_dir);

    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    app.window = SDL_CreateWindow("PC-FX / PC-FXGA", 1280, 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(!app.window)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    app.renderer = SDL_CreateRenderer(app.window, NULL);
    if(!app.renderer)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderVSync(app.renderer, 1);
    if(app.fullscreen)
        SDL_SetWindowFullscreen(app.window, true);

    app.game_tex = SDL_CreateTexture(app.renderer, PCFX_SDL_TEXTURE_FORMAT, SDL_TEXTUREACCESS_STREAMING, EMU_W, EMU_H);
    if(!app.game_tex)
    {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(app.renderer);
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(app.game_tex, app.bilinear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);

    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 44100;
    app.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if(app.audio_stream)
        SDL_ResumeAudioStreamDevice(app.audio_stream);

    memset(&cfg, 0, sizeof(cfg));
    cfg.bios_dir = app.bios_dir;
    cfg.save_dir = app.save_dir;
    cfg.sound_rate = 44100;
    cfg.fast_video = app.fast_video ? 1 : 0;
    cfg.disable_3d_hardware = disable_3d_hardware ? 1 : 0;
    cfg.prefer_fxga_bios = app.system_mode;
    app.enable_3d_hardware = !disable_3d_hardware;
    app.emu = pcfx_headless_create(&cfg);
    if(!app.emu)
    {
        fprintf(stderr, "pcfx_headless_create failed\n");
        SDL_Quit();
        return 1;
    }
    pcfx_headless_set_audio_callback(app.emu, audio_cb, app.audio_stream);
    pcfx_headless_set_controller_type(app.emu, (uint8_t)app.controller_type);
    if(app.game_path[0])
    {
        char initial_media_path[PATH_MAX];
        if(!resolve_frontend_media_path(&app, app.game_path, initial_media_path, sizeof(initial_media_path), false))
        {
            fprintf(stderr, "load failed: unsupported or unreadable media archive: %s\n", app.game_path);
            pcfx_headless_destroy(app.emu);
            SDL_Quit();
            return 1;
        }
        if(!pcfx_headless_load_cd(app.emu, initial_media_path))
        {
            fprintf(stderr, "load failed: %s\n", pcfx_headless_last_error(app.emu));
            pcfx_headless_destroy(app.emu);
            SDL_Quit();
            return 1;
        }
        basename_noext(app.game_id, sizeof(app.game_id), initial_media_path);
    }
    else if(!pcfx_headless_boot_bios(app.emu))
    {
        fprintf(stderr, "BIOS boot failed: %s\n", pcfx_headless_last_error(app.emu));
        pcfx_headless_destroy(app.emu);
        SDL_Quit();
        return 1;
    }

    {
        int gamepad_count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&gamepad_count);
        if(ids && gamepad_count > 0)
            app.gamepad = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
    }

    app.next_frame_ms = (double)now_ms();
    set_message(&app, "Loaded %s", app.game_id);

    while(app.running)
    {
        SDL_Event e;
        const bool* keys;
        double t;

        while(SDL_PollEvent(&e))
            handle_event(&app, &e);

        perform_pending_load_game(&app);
        perform_pending_swap_disc(&app);

        keys = SDL_GetKeyboardState(NULL);
        if(app.paused || app.menu_visible)
        {
            pcfx_headless_set_pad(app.emu, 0, 0);
            pcfx_headless_set_pad(app.emu, 1, 0);
            pcfx_headless_set_mouse(app.emu, 0, 0, 0);
        }
        else if(app.controller_type == 1)
        {
            pcfx_headless_set_controller_type(app.emu, 1);
            pcfx_headless_set_mouse(app.emu, app.mouse_dx, app.mouse_dy, app.mouse_buttons);
            pcfx_headless_set_pad(app.emu, 0, 0);
            pcfx_headless_set_pad(app.emu, 1, collect_pad_state(&app, keys, 1));
            app.mouse_dx = 0;
            app.mouse_dy = 0;
        }
        else
        {
            pcfx_headless_set_controller_type(app.emu, 0);
            pcfx_headless_set_pad(app.emu, 0, collect_pad_state(&app, keys, 0));
            pcfx_headless_set_pad(app.emu, 1, collect_pad_state(&app, keys, 1));
            pcfx_headless_set_mouse(app.emu, 0, 0, 0);
        }

        t = (double)now_ms();
        if(!app.paused && !app.menu_visible)
        {
            int ran = 0;
            while(t + 1.0 >= app.next_frame_ms && ran < 4)
            {
                if(!pcfx_headless_run_frame(app.emu))
                {
                    set_message(&app, "Emulation error: %s", pcfx_headless_last_error(app.emu));
                    app.running = false;
                    break;
                }
                app.next_frame_ms += 1000.0 / PCFX_FPS;
                ran++;
            }
            if(ran == 4 && t > app.next_frame_ms + 500.0)
                app.next_frame_ms = t;
        }
        else
            app.next_frame_ms = t + 1000.0 / PCFX_FPS;

        render_game(&app);
        SDL_Delay(1);
    }

    release_mouse_capture(&app);
    if(app.gamepad)
        SDL_CloseGamepad(app.gamepad);
    if(app.audio_stream)
        SDL_DestroyAudioStream(app.audio_stream);
    if(app.game_tex)
        SDL_DestroyTexture(app.game_tex);
    if(app.emu)
        pcfx_headless_destroy(app.emu);
    if(app.renderer)
        SDL_DestroyRenderer(app.renderer);
    if(app.window)
        SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
