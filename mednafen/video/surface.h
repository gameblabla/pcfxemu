#ifndef __MDFN_SURFACE_H
#define __MDFN_SURFACE_H

#if defined(WANT_32BPP)
#define RED_SHIFT 16
#define GREEN_SHIFT 8
#define BLUE_SHIFT 0
#define ALPHA_SHIFT 24
#define MAKECOLOR(r, g, b, a) ((r << RED_SHIFT) | (g << GREEN_SHIFT) | (b << BLUE_SHIFT) | (a << ALPHA_SHIFT))
#define CBSHIFT 8
#define CRSHIFT 0
#define RSHIFT_MEDNAFEN 16
#define GSHIFT_MEDNAFEN 8
#define BSHIFT_MEDNAFEN 0
#elif defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
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
#define MAKECOLOR(r, g, b, a) (((r >> RED_EXPAND) << RED_SHIFT) | ((g >> GREEN_EXPAND) << GREEN_SHIFT) | ((b >> BLUE_EXPAND) << BLUE_SHIFT))
#define CBSHIFT 8
#define CRSHIFT 0
#define RSHIFT_MEDNAFEN 16
#define GSHIFT_MEDNAFEN 8
#define BSHIFT_MEDNAFEN 0
#elif defined(WANT_16BPP) && !defined(FRONTEND_SUPPORTS_RGB565)
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
#define MAKECOLOR(r, g, b, a) (((r >> RED_EXPAND) << RED_SHIFT) | ((g >> GREEN_EXPAND) << GREEN_SHIFT) | ((b >> BLUE_EXPAND) << BLUE_SHIFT))
#define CBSHIFT 8
#define CRSHIFT 0
#define RSHIFT_MEDNAFEN 16
#define GSHIFT_MEDNAFEN 8
#define BSHIFT_MEDNAFEN 0
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
