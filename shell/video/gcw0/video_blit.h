#ifndef VIDEO_BLIT_H
#define VIDEO_BLIT_H

#include <SDL/SDL.h>

#define HOST_WIDTH_RESOLUTION sdl_screen->w
#define HOST_HEIGHT_RESOLUTION sdl_screen->h

#define BACKBUFFER_WIDTH_RESOLUTION backbuffer->w
#define BACKBUFFER_HEIGHT_RESOLUTION backbuffer->h

extern SDL_Surface *sdl_screen, *backbuffer;

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


#endif
