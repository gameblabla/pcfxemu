#ifndef __MDFN_SURFACE_H
#define __MDFN_SURFACE_H

#include <stdint.h>
#include "../mednafen-types.h"

#if defined(WANT_32BPP)
typedef uint32_t MDFN_Pixel;
#define MDFN_VIDEO_BYTES_PER_PIXEL 4
/*
 * 32-bit frontend formats.
 *
 * Default WANT_32BPP keeps the historical Mednafen integer layout
 * 0xAARRGGBB, suitable for SDL_PIXELFORMAT_ARGB8888/XRGB8888 style users.
 * FRONTEND_SUPPORTS_RGBA8888 selects little-endian RGBA byte order by using
 * integer layout 0xAABBGGRR.  This is the best WASM/browser ImageData ABI:
 * a Uint8ClampedArray can consume the frame without per-pixel RGB565 unpacking.
 */
#if defined(FRONTEND_SUPPORTS_RGBA8888)
#define RED_SHIFT 0
#define GREEN_SHIFT 8
#define BLUE_SHIFT 16
#define ALPHA_SHIFT 24
#define MAKECOLOR(r, g, b, a) ((((uint32_t)(r)) << RED_SHIFT) | (((uint32_t)(g)) << GREEN_SHIFT) | (((uint32_t)(b)) << BLUE_SHIFT) | (((uint32_t)(a)) << ALPHA_SHIFT))
#define MDFN_PIXEL_FORMAT_RGBA8888 1
#elif defined(FRONTEND_SUPPORTS_XRGB8888)
#define RED_SHIFT 16
#define GREEN_SHIFT 8
#define BLUE_SHIFT 0
#define ALPHA_SHIFT 24
#define MAKECOLOR(r, g, b, a) ((((uint32_t)(r)) << RED_SHIFT) | (((uint32_t)(g)) << GREEN_SHIFT) | (((uint32_t)(b)) << BLUE_SHIFT) | 0xFF000000u)
#define MDFN_PIXEL_FORMAT_XRGB8888 1
#else
#define RED_SHIFT 16
#define GREEN_SHIFT 8
#define BLUE_SHIFT 0
#define ALPHA_SHIFT 24
#define MAKECOLOR(r, g, b, a) ((((uint32_t)(r)) << RED_SHIFT) | (((uint32_t)(g)) << GREEN_SHIFT) | (((uint32_t)(b)) << BLUE_SHIFT) | (((uint32_t)(a)) << ALPHA_SHIFT))
#define MDFN_PIXEL_FORMAT_ARGB8888 1
#endif
#define CBSHIFT 8
#define CRSHIFT 0
#define RSHIFT_MEDNAFEN 16
#define GSHIFT_MEDNAFEN 8
#define BSHIFT_MEDNAFEN 0
#elif defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
typedef uint16_t MDFN_Pixel;
#define MDFN_VIDEO_BYTES_PER_PIXEL 2
/* 16bit color - RGB565 */
#define RED_MASK  0xf800
#define GREEN_MASK 0x7e0
#define BLUE_MASK 0x1f
#define RED_EXPAND 3
#define GREEN_EXPAND 2
#define BLUE_EXPAND 3
#define RED_SHIFT 11
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define MAKECOLOR(r, g, b, a) (MDFN_Pixel)((((r) >> RED_EXPAND) << RED_SHIFT) | (((g) >> GREEN_EXPAND) << GREEN_SHIFT) | (((b) >> BLUE_EXPAND) << BLUE_SHIFT))
#define CBSHIFT 8
#define CRSHIFT 0
#define RSHIFT_MEDNAFEN 16
#define GSHIFT_MEDNAFEN 8
#define BSHIFT_MEDNAFEN 0
#define MDFN_PIXEL_FORMAT_RGB565 1
#elif defined(WANT_16BPP) && !defined(FRONTEND_SUPPORTS_RGB565)
typedef uint16_t MDFN_Pixel;
#define MDFN_VIDEO_BYTES_PER_PIXEL 2
/* 16bit color - RGB555 */
#define RED_MASK  0x7c00
#define GREEN_MASK 0x3e0
#define BLUE_MASK 0x1f
#define RED_EXPAND 3
#define GREEN_EXPAND 3
#define BLUE_EXPAND 3
#define RED_SHIFT 10
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define MAKECOLOR(r, g, b, a) (MDFN_Pixel)((((r) >> RED_EXPAND) << RED_SHIFT) | (((g) >> GREEN_EXPAND) << GREEN_SHIFT) | (((b) >> BLUE_EXPAND) << BLUE_SHIFT))
#define CBSHIFT 8
#define CRSHIFT 0
#define RSHIFT_MEDNAFEN 16
#define GSHIFT_MEDNAFEN 8
#define BSHIFT_MEDNAFEN 0
#define MDFN_PIXEL_FORMAT_RGB555 1
#else
#error "Select a supported video format: WANT_16BPP or WANT_32BPP."
#endif

struct MDFN_PaletteEntry
{
 uint8 r, g, b;
};

typedef struct
{
 int32 x, y, w, h;
} MDFN_Rect;

enum
{
 MDFN_COLORSPACE_RGB = 0,
 MDFN_COLORSPACE_YCbCr = 1,
 MDFN_COLORSPACE_YUV = 2, // TODO, maybe.
};

#endif
