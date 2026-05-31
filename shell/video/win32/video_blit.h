#ifndef PCFX_WIN32_VIDEO_BLIT_H
#define PCFX_WIN32_VIDEO_BLIT_H

#include <stdint.h>
#include "mednafen/video/surface.h"

/*
 * This header is included by core PC-FX files such as king.c.  Do not include
 * windows.h here: MinGW exposes min/max macros from the Win32 headers unless
 * every includer has opted out first, and king.c defines its own max() helper.
 * Forward-declare the small set of Win32 handle types used by the frontend
 * entry points instead.  Translation units that need the real definitions
 * include windows.h before or after this header.
 */
#ifndef _WINDEF_
typedef struct HWND__ *HWND;
typedef struct HDC__ *HDC;
typedef struct tagRECT RECT;
#endif

#define PCFX_WIN32_FB_WIDTH 512
#define PCFX_WIN32_FB_HEIGHT 240

#define HOST_WIDTH_RESOLUTION  PCFX_WIN32_FB_WIDTH
#define HOST_HEIGHT_RESOLUTION PCFX_WIN32_FB_HEIGHT
#define BACKBUFFER_WIDTH_RESOLUTION  PCFX_WIN32_FB_WIDTH
#define BACKBUFFER_HEIGHT_RESOLUTION PCFX_WIN32_FB_HEIGHT

#ifdef __cplusplus
extern "C" {
#endif

extern const uint32_t internal_pitch;
#if defined(WANT_32BPP)
extern uint32_t* __restrict__ internal_pix;
#elif defined(WANT_16BPP)
extern uint16_t* __restrict__ internal_pix;
#else
#error "The Win32 backend supports WANT_16BPP or WANT_32BPP only."
#endif

void Init_Video(void);
void Set_Video_Menu(void);
void Set_Video_InGame(void);
void Video_Close(void);
void Update_Video_Menu(void);
void Update_Video_Ingame(void);
void Clear_Video(void);

void PCFX_Win32_SetWindow(HWND hwnd);
void PCFX_Win32_SetFullWidth(int fxga_active);
void PCFX_Win32_SetDisplayWidth(int width);

enum PCFX_Win32_VideoScaleMode {
    PCFX_WIN32_SCALE_FIXED = 0,
    PCFX_WIN32_SCALE_STRETCH = 1,
    PCFX_WIN32_SCALE_ASPECT = 2,
    PCFX_WIN32_SCALE_INTEGER = 3
};

enum PCFX_Win32_VideoBackend {
    PCFX_WIN32_VIDEO_D3D11 = 0,
    PCFX_WIN32_VIDEO_GDI = 1
};

enum PCFX_Win32_FullscreenMode {
    PCFX_WIN32_FULLSCREEN_EXCLUSIVE = 0,
    PCFX_WIN32_FULLSCREEN_BORDERLESS = 1
};

void PCFX_Win32_SetScaleMode(int scale, int mode, int smooth);
void PCFX_Win32_SetPresentationFullscreen(int fullscreen);
void PCFX_Win32_SetFullscreenMode(int mode);
int  PCFX_Win32_GetFullscreenMode(void);
void PCFX_Win32_SetClientSize(int width, int height);
void PCFX_Win32_GetScaleMode(int* scale, int* mode, int* smooth);
void PCFX_Win32_SetVideoBackend(int backend);
int  PCFX_Win32_GetVideoBackend(void);
const char* PCFX_Win32_VideoBackendName(int backend);
const char* PCFX_Win32_VideoLastError(void);
void PCFX_Win32_Paint(HDC hdc, const RECT* paint_rect);
void PCFX_Win32_ForceRedraw(void);
int  PCFX_Win32_GetDisplayWidth(void);
int  PCFX_Win32_GetDisplayHeight(void);
int  PCFX_Win32_GetBytesPerPixel(void);
const char* PCFX_Win32_GetPixelFormatName(void);

#ifdef __cplusplus
}
#endif

#endif
