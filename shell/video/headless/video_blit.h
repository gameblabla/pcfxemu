#ifndef VIDEO_BLIT_H
#define VIDEO_BLIT_H

#include <stdint.h>
#include "surface.h"

#define HOST_WIDTH_RESOLUTION 512
#define HOST_HEIGHT_RESOLUTION 240
#define BACKBUFFER_WIDTH_RESOLUTION 512
#define BACKBUFFER_HEIGHT_RESOLUTION 240

#ifdef __cplusplus
extern "C" {
#endif

extern const uint32_t internal_pitch;
extern MDFN_Pixel* __restrict__ internal_pix;

void Init_Video(void);
void Set_Video_Menu(void);
void Set_Video_InGame(void);
void Video_Close(void);
void Update_Video_Menu(void);
void Update_Video_Ingame(void);
void Clear_Video(void);

void pcfx_headless_video_set_full_width(int full_width);
void pcfx_headless_video_set_display_width(int width);
const MDFN_Pixel* pcfx_headless_video_pixels(int* width, int* height, int* pitch_pixels);
int pcfx_headless_video_bytes_per_pixel(void);
int pcfx_headless_video_pixel_format(void);
const uint16_t* pcfx_headless_video_rgb565(int* width, int* height, int* pitch_pixels);
void pcfx_headless_video_get_display_rect(int* x, int* y, int* w, int* h);

#ifdef __cplusplus
}
#endif

#endif
