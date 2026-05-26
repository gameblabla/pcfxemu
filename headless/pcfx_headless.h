#ifndef PCFX_HEADLESS_H
#define PCFX_HEADLESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PCFX_Headless PCFX_Headless;
typedef void (*PCFX_HeadlessAudioCallback)(void* userdata, const int16_t* samples, uint32_t frames);

enum
{
    PCFX_PAD_A      = 1u << 0,
    PCFX_PAD_B      = 1u << 1,
    PCFX_PAD_C      = 1u << 2,
    PCFX_PAD_X      = 1u << 3,
    PCFX_PAD_Y      = 1u << 4,
    PCFX_PAD_Z      = 1u << 5,
    PCFX_PAD_SELECT = 1u << 6,
    PCFX_PAD_START  = 1u << 7,
    PCFX_PAD_UP     = 1u << 8,
    PCFX_PAD_RIGHT  = 1u << 9,
    PCFX_PAD_DOWN   = 1u << 10,
    PCFX_PAD_LEFT   = 1u << 11
};

typedef struct PCFX_HeadlessConfig
{
    const char* bios_dir;
    const char* save_dir;
    int sound_rate;
    int fast_video; /* Nonzero selects the legacy fast RAINBOW backend. */
} PCFX_HeadlessConfig;

PCFX_Headless* pcfx_headless_create(const PCFX_HeadlessConfig* config);
void pcfx_headless_destroy(PCFX_Headless* emu);

int pcfx_headless_load_cd(PCFX_Headless* emu, const char* cd_path);
int pcfx_headless_run_frame(PCFX_Headless* emu);
int pcfx_headless_run_frames(PCFX_Headless* emu, uint64_t frames);
void pcfx_headless_set_audio_callback(PCFX_Headless* emu, PCFX_HeadlessAudioCallback callback, void* userdata);

void pcfx_headless_set_pad(PCFX_Headless* emu, unsigned player, uint16_t buttons);
uint16_t pcfx_headless_get_pad(PCFX_Headless* emu, unsigned player);

const uint16_t* pcfx_headless_get_rgb565(const PCFX_Headless* emu, int* width, int* height, int* pitch_pixels);
void pcfx_headless_get_display_rect(const PCFX_Headless* emu, int* x, int* y, int* width, int* height);
uint64_t pcfx_headless_frame_count(const PCFX_Headless* emu);
uint64_t pcfx_headless_audio_frame_count(const PCFX_Headless* emu);
const char* pcfx_headless_last_error(const PCFX_Headless* emu);
int pcfx_headless_using_fast_video(const PCFX_Headless* emu);

int pcfx_headless_save_screenshot_ppm(PCFX_Headless* emu, const char* path);
int pcfx_headless_open_y4m(PCFX_Headless* emu, const char* path);
int pcfx_headless_close_y4m(PCFX_Headless* emu);
int pcfx_headless_open_wav(PCFX_Headless* emu, const char* path);
int pcfx_headless_close_wav(PCFX_Headless* emu);

int pcfx_headless_dump_memory(PCFX_Headless* emu, const char* region, const char* path);
int pcfx_headless_save_state(PCFX_Headless* emu, const char* path);
int pcfx_headless_load_state(PCFX_Headless* emu, const char* path);

#ifdef __cplusplus
}
#endif

#endif
