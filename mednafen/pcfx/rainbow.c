#include "pcfx.h"
#include "rainbow.h"
#include "rainbow_backend.h"
#include "../settings.h"

static bool RAINBOW_UsingFastBackend = false;

bool RAINBOW_IsFastBackend(void)
{
 return RAINBOW_UsingFastBackend;
}

void RAINBOW_Write8(uint32 A, uint8 V)
{
 if(RAINBOW_UsingFastBackend) RAINBOW_Fast_Write8(A, V);
 else RAINBOW_Accurate_Write8(A, V);
}

void RAINBOW_Write16(uint32 A, uint16 V)
{
 if(RAINBOW_UsingFastBackend) RAINBOW_Fast_Write16(A, V);
 else RAINBOW_Accurate_Write16(A, V);
}

void RAINBOW_ForceTransferReset(void)
{
 if(RAINBOW_UsingFastBackend) RAINBOW_Fast_ForceTransferReset();
 else RAINBOW_Accurate_ForceTransferReset();
}

void RAINBOW_SwapBuffers(void)
{
 if(RAINBOW_UsingFastBackend) RAINBOW_Fast_SwapBuffers();
 else RAINBOW_Accurate_SwapBuffers();
}

void RAINBOW_DecodeBlock(bool arg_FirstDecode, bool Skip)
{
 if(RAINBOW_UsingFastBackend) RAINBOW_Fast_DecodeBlock(arg_FirstDecode, Skip);
 else RAINBOW_Accurate_DecodeBlock(arg_FirstDecode, Skip);
}

int RAINBOW_FetchRaster(uint32 *linebuffer, uint32 layer_or, uint32 *palette_ptr)
{
 return RAINBOW_UsingFastBackend ?
  RAINBOW_Fast_FetchRaster(linebuffer, layer_or, palette_ptr) :
  RAINBOW_Accurate_FetchRaster(linebuffer, layer_or, palette_ptr);
}

int RAINBOW_StateAction(StateMem *sm, int load, int data_only)
{
 return RAINBOW_UsingFastBackend ?
  RAINBOW_Fast_StateAction(sm, load, data_only) :
  RAINBOW_Accurate_StateAction(sm, load, data_only);
}

bool RAINBOW_Init(bool arg_ChromaIP)
{
 RAINBOW_UsingFastBackend = setting_video_fast_fallback ? true : false;
 return RAINBOW_UsingFastBackend ? RAINBOW_Fast_Init(arg_ChromaIP) : RAINBOW_Accurate_Init(arg_ChromaIP);
}

void RAINBOW_Close(void)
{
 if(RAINBOW_UsingFastBackend) RAINBOW_Fast_Close();
 else RAINBOW_Accurate_Close();
}

void RAINBOW_Reset(void)
{
 if(RAINBOW_UsingFastBackend) RAINBOW_Fast_Reset();
 else RAINBOW_Accurate_Reset();
}
