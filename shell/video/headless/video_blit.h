#ifndef VIDEO_BLIT_H
#define VIDEO_BLIT_H

#include <stdint.h>

#define HOST_WIDTH_RESOLUTION 320
#define HOST_HEIGHT_RESOLUTION 240
#define BACKBUFFER_WIDTH_RESOLUTION 320
#define BACKBUFFER_HEIGHT_RESOLUTION 240

extern const uint32_t internal_pitch;
#if defined(WANT_32BPP)
extern uint32_t* __restrict__ internal_pix;
#elif defined(WANT_16BPP)
extern uint16_t* __restrict__ internal_pix;
#elif defined(WANT_8BPP)
extern uint8_t* __restrict__ internal_pix;
#endif

void Init_Video(void);
void Set_Video_Menu(void);
void Set_Video_InGame(void);
void Video_Close(void);
void Update_Video_Menu(void);
void Update_Video_Ingame(void);
void Clear_Video(void);

#ifdef __cplusplus
extern "C" {
#endif
const uint16_t* pcfx_headless_video_rgb565(int* width, int* height, int* pitch_pixels);
void pcfx_headless_video_get_display_rect(int* x, int* y, int* w, int* h);
void pcfx_headless_video_note_full_width_line(void);
#ifdef __cplusplus
}
#endif

#endif

#ifdef __cplusplus
extern "C" {
#endif
void pcfx_headless_video_set_full_width(int full_width);
#ifdef __cplusplus
}
#endif
