#include "pcfx_headless.h"

#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <string>

#include "shared.h"
#include "menu.h"
#include "config.h"
#include "settings.h"

extern "C" void pcfx_headless_set_paths(const char* bios_dir, const char* save_dir);
extern "C" void pcfx_headless_input_set_pad(uint16_t state);
extern "C" uint16_t pcfx_headless_input_get_pad(void);
extern "C" const uint16_t* pcfx_headless_video_rgb565(int* width, int* height, int* pitch_pixels);
extern "C" void pcfx_headless_video_get_display_rect(int* x, int* y, int* width, int* height);

extern void Emu_Init(void);
extern void Load_Game_Memory(char* path);
extern void Emulation_Run(void);
extern void Init_Video(void);
extern uint32_t Audio_Init(void);
extern void Video_Close(void);
extern void Audio_Close(void);
extern void SaveState(char* path, uint_fast8_t state);
extern "C" bool retro_serialize(void* data, size_t size);
extern "C" bool retro_unserialize(const void* data, size_t size);
extern "C" size_t retro_serialize_size(void);
extern char GameName_emu[256];
extern bool RAINBOW_IsFastBackend(void);
extern "C" void PCFX_Headless_CoreClose(void);
extern "C" uint8_t* PCFX_Headless_CoreRAM(size_t* size);
extern "C" uint8_t* PCFX_Headless_CoreSaveRAM(size_t* size);

struct PCFX_Headless
{
    bool initialized;
    bool loaded;
    uint64_t frame_count;
    uint64_t audio_frame_count;
    FILE* y4m;
    FILE* wav;
    uint64_t wav_data_bytes;
    char error[512];
    PCFX_HeadlessAudioCallback audio_callback;
    void* audio_callback_userdata;
};

static PCFX_Headless* g_active;

static int set_error(PCFX_Headless* emu, const char* fmt, ...)
{
    if(!emu)
        return 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(emu->error, sizeof(emu->error), fmt, ap);
    va_end(ap);
    return 0;
}

static bool file_exists(const char* path)
{
    struct stat st;
    return path && !stat(path, &st) && S_ISREG(st.st_mode);
}

static bool dir_exists(const char* path)
{
    struct stat st;
    return path && !stat(path, &st) && S_ISDIR(st.st_mode);
}

static bool has_huexe_extension(const char* path)
{
    if(!path)
        return false;
    const char* dot = strrchr(path, '.');
    return dot && (!strcasecmp(dot, ".ex") || !strcasecmp(dot, ".exe"));
}

static bool resolve_homebrew_path(const char* input, std::string* out)
{
    if(!input || !out)
        return false;
    if(file_exists(input))
    {
        *out = input;
        return true;
    }
    if(!dir_exists(input))
        return false;

    DIR* dp = opendir(input);
    if(!dp)
        return false;
    std::string best;
    struct dirent* ent = NULL;
    while((ent = readdir(dp)))
    {
        if(ent->d_name[0] == '.')
            continue;
        if(!has_huexe_extension(ent->d_name))
            continue;
        std::string candidate = std::string(input) + "/" + ent->d_name;
        if(file_exists(candidate.c_str()) && (best.empty() || candidate < best))
            best = candidate;
    }
    closedir(dp);
    if(best.empty())
        return false;
    *out = best;
    return true;
}

static void make_game_name(const char* path)
{
    const char* slash = strrchr(path, '/');
#ifdef _WIN32
    const char* bslash = strrchr(path, '\\');
    if(!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    const char* base = slash ? slash + 1 : path;
    snprintf(GameName_emu, 256, "%s", base ? base : "pcfx");
}

static void rgb565_to_rgb(uint16_t p, uint8_t* r, uint8_t* g, uint8_t* b)
{
    *r = (uint8_t)((((p >> 11) & 0x1f) * 255 + 15) / 31);
    *g = (uint8_t)((((p >> 5) & 0x3f) * 255 + 31) / 63);
    *b = (uint8_t)(((p & 0x1f) * 255 + 15) / 31);
}

static uint8_t clamp_u8(int v)
{
    if(v < 0) return 0;
    if(v > 255) return 255;
    return (uint8_t)v;
}

static int write_y4m_frame(PCFX_Headless* emu)
{
    if(!emu || !emu->y4m)
        return 1;

    int w = 0, h = 0, pitch = 0;
    const uint16_t* pix = pcfx_headless_video_rgb565(&w, &h, &pitch);
    if(!pix)
        return set_error(emu, "no video buffer available");

    if(fputs("FRAME\n", emu->y4m) < 0)
        return set_error(emu, "failed to write Y4M frame marker");

    const size_t plane_size = (size_t)w * (size_t)h;
    uint8_t* y = (uint8_t*)malloc(plane_size * 3);
    if(!y)
        return set_error(emu, "out of memory writing Y4M frame");
    uint8_t* u = y + plane_size;
    uint8_t* v = u + plane_size;

    for(int yy = 0; yy < h; yy++)
    {
        for(int xx = 0; xx < w; xx++)
        {
            uint8_t r, g, b;
            rgb565_to_rgb(pix[yy * pitch + xx], &r, &g, &b);
            const size_t off = (size_t)yy * (size_t)w + (size_t)xx;
            y[off] = clamp_u8(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
            u[off] = clamp_u8(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            v[off] = clamp_u8(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }

    bool ok = fwrite(y, 1, plane_size * 3, emu->y4m) == plane_size * 3;
    free(y);
    if(!ok)
        return set_error(emu, "failed to write Y4M frame payload");
    return 1;
}

static void put_le16(FILE* fp, uint16_t v)
{
    fputc(v & 0xff, fp);
    fputc((v >> 8) & 0xff, fp);
}

static void put_le32(FILE* fp, uint32_t v)
{
    fputc(v & 0xff, fp);
    fputc((v >> 8) & 0xff, fp);
    fputc((v >> 16) & 0xff, fp);
    fputc((v >> 24) & 0xff, fp);
}

static void write_wav_header(FILE* fp, uint64_t data_bytes)
{
    const uint32_t clipped_data = data_bytes > 0xffffffffULL ? 0xffffffffU : (uint32_t)data_bytes;
    const uint32_t riff_size = clipped_data > 0xfffffff7U ? 0xffffffffU : clipped_data + 36U;
    fseek(fp, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, fp);
    put_le32(fp, riff_size);
    fwrite("WAVEfmt ", 1, 8, fp);
    put_le32(fp, 16);
    put_le16(fp, 1);       // PCM
    put_le16(fp, 2);       // stereo
    put_le32(fp, SOUND_OUTPUT_FREQUENCY);
    put_le32(fp, SOUND_OUTPUT_FREQUENCY * 2 * 2);
    put_le16(fp, 4);       // block align
    put_le16(fp, 16);      // bits
    fwrite("data", 1, 4, fp);
    put_le32(fp, clipped_data);
}

extern "C" void pcfx_headless_audio_write(const int16_t* samples, uint32_t frames)
{
    PCFX_Headless* emu = g_active;
    if(!emu || !samples || !frames)
        return;
    emu->audio_frame_count += frames;
    if(emu->audio_callback)
        emu->audio_callback(emu->audio_callback_userdata, samples, frames);
    if(emu->wav)
    {
        const size_t bytes = (size_t)frames * 2u * sizeof(int16_t);
        fwrite(samples, 1, bytes, emu->wav);
        emu->wav_data_bytes += bytes;
    }
}

PCFX_Headless* pcfx_headless_create(const PCFX_HeadlessConfig* config)
{
    if(g_active)
        return NULL;

    PCFX_Headless* emu = (PCFX_Headless*)calloc(1, sizeof(PCFX_Headless));
    if(!emu)
        return NULL;

    const char* bios_dir = (config && config->bios_dir) ? config->bios_dir : ".";
    const char* save_dir = (config && config->save_dir) ? config->save_dir : bios_dir;
    pcfx_headless_set_paths(bios_dir, save_dir);
    MDFN_SetPCFXFastVideo((config && config->fast_video) ? 1 : setting_video_fast_fallback);

    g_active = emu;
    Emu_Init();
    Init_Video();
    Audio_Init();
    emu->initialized = true;
    return emu;
}

void pcfx_headless_destroy(PCFX_Headless* emu)
{
    if(!emu)
        return;
    pcfx_headless_close_y4m(emu);
    pcfx_headless_close_wav(emu);
    if(emu->initialized)
    {
        PCFX_Headless_CoreClose();
        Audio_Close();
        Video_Close();
    }
    if(g_active == emu)
        g_active = NULL;
    free(emu);
}

int pcfx_headless_load_cd(PCFX_Headless* emu, const char* cd_path)
{
    if(!emu)
        return 0;
    if(emu != g_active)
        return set_error(emu, "this build supports one active emulator instance at a time");
    std::string resolved_path;
    if(!resolve_homebrew_path(cd_path, &resolved_path))
        return set_error(emu, "game path is not a regular file or a directory containing .EX/.EXE: %s", cd_path ? cd_path : "(null)");
    char bios_path[512];
    snprintf(bios_path, sizeof(bios_path), "%s/pcfx.rom", home_path);
    if(!file_exists(bios_path))
        return set_error(emu, "missing PC-FX BIOS: %s", bios_path);

    make_game_name(resolved_path.c_str());
    std::string mutable_path(resolved_path);
    Load_Game_Memory(&mutable_path[0]);
    emu->loaded = true;
    return 1;
}


void pcfx_headless_set_audio_callback(PCFX_Headless* emu, PCFX_HeadlessAudioCallback callback, void* userdata)
{
    if(!emu)
        return;
    emu->audio_callback = callback;
    emu->audio_callback_userdata = userdata;
}

int pcfx_headless_run_frame(PCFX_Headless* emu)
{
    if(!emu || !emu->loaded)
        return set_error(emu, "no CD loaded");
    Emulation_Run();
    emu->frame_count++;
    if(emu->y4m && !write_y4m_frame(emu))
        return 0;
    return 1;
}

int pcfx_headless_run_frames(PCFX_Headless* emu, uint64_t frames)
{
    for(uint64_t i = 0; i < frames; i++)
        if(!pcfx_headless_run_frame(emu))
            return 0;
    return 1;
}

void pcfx_headless_set_pad(PCFX_Headless* emu, unsigned player, uint16_t buttons)
{
    (void)emu;
    if(player == 0)
        pcfx_headless_input_set_pad(buttons);
}

uint16_t pcfx_headless_get_pad(PCFX_Headless* emu, unsigned player)
{
    (void)emu;
    if(player == 0)
        return pcfx_headless_input_get_pad();
    return 0;
}

const uint16_t* pcfx_headless_get_rgb565(const PCFX_Headless* emu, int* width, int* height, int* pitch_pixels)
{
    (void)emu;
    return pcfx_headless_video_rgb565(width, height, pitch_pixels);
}

void pcfx_headless_get_display_rect(const PCFX_Headless* emu, int* x, int* y, int* width, int* height)
{
    (void)emu;
    pcfx_headless_video_get_display_rect(x, y, width, height);
}

uint64_t pcfx_headless_frame_count(const PCFX_Headless* emu)
{
    return emu ? emu->frame_count : 0;
}

uint64_t pcfx_headless_audio_frame_count(const PCFX_Headless* emu)
{
    return emu ? emu->audio_frame_count : 0;
}

const char* pcfx_headless_last_error(const PCFX_Headless* emu)
{
    return (emu && emu->error[0]) ? emu->error : "";
}

int pcfx_headless_using_fast_video(const PCFX_Headless* emu)
{
    (void)emu;
    return RAINBOW_IsFastBackend() ? 1 : 0;
}

int pcfx_headless_save_screenshot_ppm(PCFX_Headless* emu, const char* path)
{
    if(!emu || !path)
        return 0;
    int w = 0, h = 0, pitch = 0;
    const uint16_t* pix = pcfx_headless_video_rgb565(&w, &h, &pitch);
    if(!pix)
        return set_error(emu, "no video buffer available");
    FILE* fp = fopen(path, "wb");
    if(!fp)
        return set_error(emu, "failed to open screenshot for write: %s", path);
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    for(int yy = 0; yy < h; yy++)
    {
        for(int xx = 0; xx < w; xx++)
        {
            uint8_t rgb[3];
            rgb565_to_rgb(pix[yy * pitch + xx], &rgb[0], &rgb[1], &rgb[2]);
            fwrite(rgb, 1, 3, fp);
        }
    }
    const bool ok = !ferror(fp);
    fclose(fp);
    if(!ok)
        return set_error(emu, "failed while writing screenshot: %s", path);
    return 1;
}

int pcfx_headless_open_y4m(PCFX_Headless* emu, const char* path)
{
    if(!emu || !path)
        return 0;
    pcfx_headless_close_y4m(emu);
    emu->y4m = fopen(path, "wb");
    if(!emu->y4m)
        return set_error(emu, "failed to open Y4M for write: %s", path);
    int w = 0, h = 0, pitch = 0;
    pcfx_headless_video_rgb565(&w, &h, &pitch);
    fprintf(emu->y4m, "YUV4MPEG2 W%d H%d F60000:1001 Ip A1:1 C444\n", w, h);
    return ferror(emu->y4m) ? set_error(emu, "failed to write Y4M header: %s", path) : 1;
}

int pcfx_headless_close_y4m(PCFX_Headless* emu)
{
    if(!emu || !emu->y4m)
        return 1;
    const int ret = fclose(emu->y4m);
    emu->y4m = NULL;
    return ret == 0 ? 1 : set_error(emu, "failed to close Y4M file");
}

int pcfx_headless_open_wav(PCFX_Headless* emu, const char* path)
{
    if(!emu || !path)
        return 0;
    pcfx_headless_close_wav(emu);
    emu->wav = fopen(path, "wb+");
    if(!emu->wav)
        return set_error(emu, "failed to open WAV for write: %s", path);
    emu->wav_data_bytes = 0;
    write_wav_header(emu->wav, 0);
    fseek(emu->wav, 44, SEEK_SET);
    return 1;
}

int pcfx_headless_close_wav(PCFX_Headless* emu)
{
    if(!emu || !emu->wav)
        return 1;
    write_wav_header(emu->wav, emu->wav_data_bytes);
    const int ret = fclose(emu->wav);
    emu->wav = NULL;
    return ret == 0 ? 1 : set_error(emu, "failed to close WAV file");
}

int pcfx_headless_dump_memory(PCFX_Headless* emu, const char* region, const char* path)
{
    if(!emu || !region || !path)
        return 0;
    const uint8_t* data = NULL;
    size_t size = 0;
    uint8_t* owned = NULL;

    if(!strcmp(region, "ram"))
        data = PCFX_Headless_CoreRAM(&size);
    else if(!strcmp(region, "saveram") || !strcmp(region, "bram"))
        data = PCFX_Headless_CoreSaveRAM(&size);
    else if(!strcmp(region, "state"))
    {
        size = retro_serialize_size();
        if(!size)
            return set_error(emu, "state serialization returned zero bytes");
        owned = (uint8_t*)malloc(size);
        if(!owned)
            return set_error(emu, "out of memory serializing state");
        if(!retro_serialize(owned, size))
        {
            free(owned);
            return set_error(emu, "state serialization failed");
        }
        data = owned;
    }
    else
        return set_error(emu, "unknown memory region '%s' (supported: ram, saveram, state)", region);

    if(!data || !size)
    {
        free(owned);
        return set_error(emu, "memory region '%s' is unavailable", region);
    }

    FILE* fp = fopen(path, "wb");
    if(!fp)
    {
        free(owned);
        return set_error(emu, "failed to open memory dump for write: %s", path);
    }
    const bool ok = fwrite(data, 1, size, fp) == size;
    fclose(fp);
    free(owned);
    if(!ok)
        return set_error(emu, "failed while writing memory dump: %s", path);
    return 1;
}

int pcfx_headless_save_state(PCFX_Headless* emu, const char* path)
{
    if(!emu || !path)
        return 0;
    std::string mutable_path(path);
    SaveState(&mutable_path[0], 0);
    return 1;
}

int pcfx_headless_load_state(PCFX_Headless* emu, const char* path)
{
    if(!emu || !path)
        return 0;
    if(!file_exists(path))
        return set_error(emu, "state file does not exist: %s", path);
    std::string mutable_path(path);
    SaveState(&mutable_path[0], 1);
    return 1;
}
