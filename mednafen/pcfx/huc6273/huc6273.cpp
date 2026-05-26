/* Mednafen - Multi-system Emulator
 *
 * This file contains a conservative first-pass HuC6273(Aurora) implementation
 * for PC-FXGA software.  It is not a cycle-accurate renderer.  The goal here
 * is to model the command/register interface well enough for FARL/GMAKER titles
 * to leave their hardware probes and submit/display simple fixed-point geometry.
 */

#include "pcfx.h"
#include "huc6273.h"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

// Command opcodes.  Header format is OOOO PPPP LLLLLLLL.
enum
{
 OP_NOP                = 0x0,
 OP_TRIANGLE_STRIP     = 0x1,
 OP_TRIANGLE_LIST      = 0x2,
 OP_POLY_LINE          = 0x3,
 OP_LINE_LIST          = 0x4,
 OP_PUT_IMAGE          = 0x6,
 OP_READ_PIXEL         = 0x7,
 OP_WRITE_TE_REGISTERS = 0x8,
 OP_WRITE_PE_REGISTERS = 0x9,
 OP_MISC               = 0xA,
 OP_READ_TE_REGISTERS  = 0xC,
 OP_READ_PE_REGISTERS  = 0xD,
 OP_WRITE_LUT          = 0xE,
 OP_READ_LUT           = 0xF,
};

// Interrupt bits from FARL/Daifukkat documentation.
enum
{
 INT_FSY    = 1 << 1,
 INT_OVF    = 1 << 2,
 INT_AFL    = 1 << 3,
 INT_CSE    = 1 << 4,
 INT_AEMP   = 1 << 5,
 INT_PESYNC = 1 << 6,
 INT_CMDONE = 1 << 7,
 INT_SPDONE = 1 << 8,
 INT_TESYNC = 1 << 9,
 INT_RBDONE = 1 << 10,
 INT_VSY    = 1 << 11,
 INT_HSY    = 1 << 12,
 INT_VBL    = 1 << 13,
 INT_HBL    = 1 << 14,
 INT_RHIT   = 1 << 15,
};

static const int FB_W = 256;
static const int FB_H = 240;
static const int TEX_W = 256;
static const int TEX_H = 256;
static const int FIFO_CAPACITY = 32;

static uint16 FIFOControl;
static uint16 CMTBankSelect;
static uint16 CMTStartAddress;
static uint16 CMTByteCount;
static uint16 InterruptMask;
static uint16 InterruptStatus;
static uint16 ReadBack;
static uint16 HorizontalTiming, VerticalTiming;
static uint16 SCTAddress;
static uint16 SpriteControl;
static uint16 CDResult[2];
static uint16 SPWindowX[2];
static uint16 SPWindowY[2];
static uint16 MiscStatus;
static uint16 ErrorStatus;
static uint16 DisplayControl;
static uint16 StatusControl;
static uint16 RasterHit;
static uint16 TECodeControl;
static uint16 TEAddressControl;
static uint16 PixelEngineTest;
static uint16 MemoryTest;
static uint16 Results[16];
static uint16 TE[256];
static uint16 PE[16];
static uint16 LUT[256];

// HuC6273 has an internal matrix unit used heavily by FARL/GMAKER.  The
// command stream generally builds a destination matrix with Misc op A6, then
// copies it to source/object/normal matrices with A8/A9/AA/AB.  Values here
// are kept in the command/native 1.8.7 fixed-point format unless noted.
static int16 MatrixSrc4[16];
static int16 MatrixDst4[16];
static bool ObjectMatrixValid;

// 0x80510000..0x8052FFFF, halfword-addressed command/texture RAM window.
static uint16 CommandTextureMem[0x10000];

static uint16 FrameBuffer[2][FB_W * FB_H];
static uint8 FrameValid[2][FB_W * FB_H];
static int32 ZBuffer[FB_W * FB_H];
static const int TEX_BANKS = 32;
static uint16 Texture[TEX_BANKS][TEX_W * TEX_H];
static uint8 TextureValid[TEX_BANKS][TEX_W * TEX_H];
static int FrontBuffer;
static int DrawBuffer;

static std::vector<uint16> PendingFIFO;

extern uint16 FXVCE_GetPaletteRGB565(uint16 index);

static inline int16 S16(uint16 v) { return (int16)v; }
static inline uint16 Clamp16(int v) { return (uint16)(v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : v)); }

static void MatrixIdentity(int16 m[16])
{
 memset(m, 0, sizeof(int16) * 16);
 m[0] = m[5] = m[10] = m[15] = 0x0080;
}

static int16 Mul187(int16 a, int16 b)
{
 int32 v = (int32)a * (int32)b;
 v += (v >= 0) ? 0x40 : -0x40;
 v >>= 7;
 if(v < -32768) v = -32768;
 if(v >  32767) v =  32767;
 return (int16)v;
}

static void MatrixMul187(const int16 cmd[16], const int16 src[16], int16 dst[16])
{
 int16 r[16];
 for(int row = 0; row < 4; row++)
  for(int col = 0; col < 4; col++)
  {
   int32 v = 0;
   for(int k = 0; k < 4; k++)
    v += (int32)cmd[row * 4 + k] * (int32)src[k * 4 + col];
   v += (v >= 0) ? 0x40 : -0x40;
   v >>= 7;
   if(v < -32768) v = -32768;
   if(v >  32767) v =  32767;
   r[row * 4 + col] = (int16)v;
  }
 memcpy(dst, r, sizeof(r));
}

static void CopyMatrixToTEObject(const int16 m[16])
{
 const uint8 a[16] = {2,4,12,14,16,20,22,30,32,34,38,40,48,50,52,54};
 for(int i = 0; i < 16; i++)
  TE[a[i]] = (uint16)m[i];
 ObjectMatrixValid = true;
}

static void CopyMatrixToTESource(const int16 m[16])
{
 const uint8 a[16] = {97,99,101,103,105,107,109,111,113,115,117,119,121,123,125,127};
 for(int i = 0; i < 16; i++)
  TE[a[i]] = (uint16)m[i];
}

static float Matrix187(uint8 addr, float def)
{
 if(!ObjectMatrixValid)
  return def;
 return (float)S16(TE[addr]) / 128.0f;
}

static void ClearZ(void)
{
 for(int i = 0; i < FB_W * FB_H; i++)
  ZBuffer[i] = -0x7FFFFFFF;
}

static uint16 RGB444ToNative(uint16 c)
{
 // FARL examples commonly use 12-bit I/C or RGB-ish constants.  Same Game's
 // early setup uses an IC mask of 0x0FFF; interpreting this as RGB444 gives a
 // useful, deterministic first approximation for flat primitive color.
 uint8 r = ((c >> 8) & 0xF) * 17;
 uint8 g = ((c >> 4) & 0xF) * 17;
 uint8 b = ((c >> 0) & 0xF) * 17;
 return (uint16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline int32 Fixed115ToInt(uint16 v)
{
 return (int32)S16(v);
}

struct Vtx
{
 float x, y, z;
 int sx, sy;
 int32 zi;
 uint16 color;
 uint16 u, v;
};

static int TEWin(uint8 addr, int def)
{
 uint16 v = TE[addr];
 return v ? (int)S16(v) : def;
}

static void ProjectVertex(Vtx& out, uint16 xw, uint16 yw, uint16 zw, uint16 color, uint16 u = 0, uint16 v = 0)
{
 const float vx = (float)S16(xw) / 32768.0f;
 const float vy = (float)S16(yw) / 32768.0f;
 const float vz = (float)S16(zw) / 32768.0f;

 // Object matrix register layout from FARL.H: rows are not contiguous.
 const float m00 = Matrix187(2,  1.0f), m01 = Matrix187(4,  0.0f), m02 = Matrix187(12, 0.0f), m03 = Matrix187(14, 0.0f);
 const float m10 = Matrix187(16, 0.0f), m11 = Matrix187(20, 1.0f), m12 = Matrix187(22, 0.0f), m13 = Matrix187(30, 0.0f);
 const float m20 = Matrix187(32, 0.0f), m21 = Matrix187(34, 0.0f), m22 = Matrix187(38, 1.0f), m23 = Matrix187(40, 0.0f);

 float ox = m00 * vx + m01 * vy + m02 * vz + m03;
 float oy = m10 * vx + m11 * vy + m12 * vz + m13;
 float oz = m20 * vx + m21 * vy + m22 * vz + m23;

 // Window scaling registers are used by FARL's setup.  Identity defaults match
 // the documented examples: scale X by +128, scale Y by -128, translate to 128.
 const int sx_scale = TEWin(75, 128);
 const int sx_trans = TEWin(77, 128);
 const int sy_scale = TEWin(79, -128);
 const int sy_trans = TEWin(81, 128);

 out.x = ox;
 out.y = oy;
 out.z = oz;
 out.sx = (int)lrintf(ox * sx_scale + sx_trans);
 out.sy = (int)lrintf(oy * sy_scale + sy_trans);
 out.zi = (int32)lrintf(oz * 32768.0f);
 out.color = color;
 out.u = u;
 out.v = v;
}

static bool ReadXYZ(const uint16* cmd, size_t count, size_t& pos, Vtx& out, uint16 color, uint16 u = 0, uint16 v = 0)
{
 if(pos + 3 > count)
  return false;
 ProjectVertex(out, cmd[pos], cmd[pos + 1], cmd[pos + 2], color, u, v);
 pos += 3;
 return true;
}

static void FillRect(int xl, int yt, int xr, int yb, uint16 native_color)
{
 if(xl > xr) std::swap(xl, xr);
 if(yt > yb) std::swap(yt, yb);
 xl = std::max(0, std::min(FB_W - 1, xl));
 xr = std::max(0, std::min(FB_W - 1, xr));
 yt = std::max(0, std::min(FB_H - 1, yt));
 yb = std::max(0, std::min(FB_H - 1, yb));
 for(int y = yt; y <= yb; y++)
  for(int x = xl; x <= xr; x++)
  {
   const int o = y * FB_W + x;
   FrameBuffer[DrawBuffer][o] = native_color;
   FrameValid[DrawBuffer][o] = 1;
   ZBuffer[o] = 0x7FFFFFFF;
  }
}

static inline int Edge(int ax, int ay, int bx, int by, int cx, int cy)
{
 return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

static uint16 CompressIC(uint16 v, uint16 mask)
{
 uint16 out = 0;
 int ob = 0;
 for(int b = 0; b < 12; b++)
  if(mask & (1U << b))
  {
   if(v & (1U << b))
    out |= (1U << ob);
   ob++;
  }
 return out & 0x1FF;
}

static uint16 TextureWordToNative(uint16 pix, uint16 fallback)
{
 // In normal FARL use, texture pixels are 9-bit I/C values embedded in a
 // 12-bit word according to PE IC-mask.  The PC-FX VCE palette is then loaded
 // by the title with the matching I/C->YUV table.  Since this renderer is
 // overlaid after KING/VCE mixing, convert through the live VCE palette here.
 if(!(PE[3] & (1 << 10)))
 {
  const uint16 mask = PE[7] ? (PE[7] & 0x0FFF) : 0x0FC7;
  return FXVCE_GetPaletteRGB565(CompressIC(pix, mask));
 }
 return RGB444ToNative(pix);
}

static uint16 SampleTexture(uint16 u, uint16 v, uint16 fallback)
{
 const int tx = (int)((u + TE[86]) & (TEX_W - 1));
 const int ty = (int)((v + TE[88]) & (TEX_H - 1));
 const int o = ty * TEX_W + tx;
 const int bank = PE[0] & (TEX_BANKS - 1);

 if(TextureValid[bank][o])
  return TextureWordToNative(Texture[bank][o], fallback);

 // Some early FARL samples switch the texture select register during setup in
 // ways that are still not fully understood.  Falling back to bank 0 avoids
 // turning unknown-bank texture maps into solid white while preserving correct
 // output when the selected bank has data.
 if(bank && TextureValid[0][o])
  return TextureWordToNative(Texture[0][o], fallback);

 return fallback;
}

static void DrawTriangle(const Vtx& a, const Vtx& b, const Vtx& c, bool textured)
{
 const int area = Edge(a.sx, a.sy, b.sx, b.sy, c.sx, c.sy);
 if(area == 0)
  return;

 int minx = std::max(0, std::min(a.sx, std::min(b.sx, c.sx)));
 int maxx = std::min(FB_W - 1, std::max(a.sx, std::max(b.sx, c.sx)));
 int miny = std::max(0, std::min(a.sy, std::min(b.sy, c.sy)));
 int maxy = std::min(FB_H - 1, std::max(a.sy, std::max(b.sy, c.sy)));
 if(minx > maxx || miny > maxy)
  return;

 uint16 native_color = RGB444ToNative(a.color ? a.color : (TE[90] ? TE[90] : 0x0FFF));
 for(int y = miny; y <= maxy; y++)
  for(int x = minx; x <= maxx; x++)
  {
   const int w0 = Edge(b.sx, b.sy, c.sx, c.sy, x, y);
   const int w1 = Edge(c.sx, c.sy, a.sx, a.sy, x, y);
   const int w2 = Edge(a.sx, a.sy, b.sx, b.sy, x, y);
   if((area > 0 && (w0 < 0 || w1 < 0 || w2 < 0)) || (area < 0 && (w0 > 0 || w1 > 0 || w2 > 0)))
    continue;

   const int o = y * FB_W + x;
   const int32 zi = (a.zi + b.zi + c.zi) / 3;
   if(FrameValid[DrawBuffer][o] && zi <= ZBuffer[o])
    continue;
   uint16 color = native_color;
   if(textured)
   {
    const float inv_area = 1.0f / (float)area;
    const float fa = w0 * inv_area;
    const float fb = w1 * inv_area;
    const float fc = w2 * inv_area;
    uint16 uu = (uint16)lrintf(a.u * fa + b.u * fb + c.u * fc);
    uint16 vv = (uint16)lrintf(a.v * fa + b.v * fb + c.v * fc);
    color = SampleTexture(uu, vv, native_color);
   }
   FrameBuffer[DrawBuffer][o] = color;
   FrameValid[DrawBuffer][o] = 1;
   ZBuffer[o] = zi;
  }
}

static void DrawLine(int x0, int y0, int x1, int y1, uint16 color)
{
 uint16 native = RGB444ToNative(color ? color : (TE[90] ? TE[90] : 0x0FFF));
 int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
 int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
 int err = dx + dy;
 while(true)
 {
  if(x0 >= 0 && x0 < FB_W && y0 >= 0 && y0 < FB_H)
  {
   int o = y0 * FB_W + x0;
   FrameBuffer[DrawBuffer][o] = native;
   FrameValid[DrawBuffer][o] = 1;
  }
  if(x0 == x1 && y0 == y1)
   break;
  int e2 = 2 * err;
  if(e2 >= dy) { err += dy; x0 += sx; }
  if(e2 <= dx) { err += dx; y0 += sy; }
 }
}

static void Complete(uint16 bits = INT_AEMP)
{
 InterruptStatus |= bits | INT_AEMP;
 MiscStatus &= ~((1 << 3) | (1 << 4) | (1 << 11) | (1 << 12)); // PE/TE/clear/CMT busy clear
 ErrorStatus = 0x0011; // idle-ish TE/PE status; no error code.
}

static void DoWriteTE(uint8 option, const uint16* body, size_t n)
{
 switch(option)
 {
  case 0:
  {
   const uint8 a[16] = {2,4,12,14,16,20,22,30,32,34,38,40,48,50,52,54};
   bool any = false;
   for(size_t i=0;i<n && i<16;i++)
   {
    TE[a[i]] = body[i];
    any |= (body[i] != 0);
   }
   // FARL code sometimes clears TE object-matrix registers during setup.
   // Treat an all-zero write as a register clear, not as a valid projection
   // matrix, otherwise early geometry collapses to a point until the matrix
   // unit copies a real destination matrix with Misc op A9.
   if(any)
    ObjectMatrixValid = true;
   break;
  }
  case 1: { const uint8 a[9]  = {56,58,60,62,64,66,68,70,72}; for(size_t i=0;i<n && i<9;i++) TE[a[i]] = body[i]; break; }
  case 2: { const uint8 a[3]  = {74,76,78}; for(size_t i=0;i<n && i<3;i++) TE[a[i]] = body[i]; break; }
  case 3: { const uint8 a[3]  = {80,82,84}; for(size_t i=0;i<n && i<3;i++) TE[a[i]] = body[i]; break; }
  case 4: { const uint8 a[5]  = {55,57,59,61,63}; for(size_t i=0;i<n && i<5;i++) TE[a[i]] = body[i]; break; }
  case 6: if(n) TE[255] = body[0]; break;
  case 7: { const uint8 a[4]  = {65,67,69,71}; for(size_t i=0;i<n && i<4;i++) TE[a[i]] = body[i]; break; }
  case 8: { const uint8 a[4]  = {75,77,79,81}; for(size_t i=0;i<n && i<4;i++) TE[a[i]] = body[i]; break; }
  case 9: if(n) TE[73] = body[0]; break;
  case 0xA: { const uint8 a[16] = {97,99,101,103,105,107,109,111,113,115,117,119,121,123,125,127}; for(size_t i=0;i<n && i<16;i++) { TE[a[i]] = body[i]; MatrixSrc4[i] = S16(body[i]); } break; }
  case 0xB: if(n) TE[107] = body[0]; break;
  case 0xC: { const uint8 a[2]  = {86,88}; for(size_t i=0;i<n && i<2;i++) TE[a[i]] = body[i]; break; }
  case 0xD: if(n) TE[90] = body[0]; break;
  case 0xF:
   for(size_t i = 0; i + 1 < n; i += 2)
    TE[body[i] & 0xFF] = body[i + 1];
   break;
 }
 Complete(INT_TESYNC);
}

static void DoWritePE(uint8 option, const uint16* body, size_t n)
{
 if(n)
  PE[option & 0xF] = body[0];

 if((option & 0xF) == 4 && n && (body[0] & 1))
  ClearZ();

 if((option & 0xF) == 5 && n)
 {
  const uint16 v = body[0];
  // Software normally requests swap/VSync through frame control.  Use the
  // documented SWP bits loosely; hidden-clear is handled by explicit fill/clear
  // commands to avoid erasing still-pending geometry.
  if(v & 0x0007)
  {
   FrontBuffer = DrawBuffer;
   DrawBuffer ^= 1;
   memset(FrameValid[DrawBuffer], 0, sizeof(FrameValid[DrawBuffer]));
   ClearZ();
  }
  Complete(INT_PESYNC | INT_FSY | INT_VSY | INT_VBL);
 }
 else
  Complete(INT_PESYNC);
}

static void DoPutImage(uint8 option, const uint16* body, size_t n)
{
 if(option == 1 && n >= 4)
 {
  int xl = S16(body[0]), yt = S16(body[1]), xr = S16(body[2]), yb = S16(body[3]);
  if(xl > xr) std::swap(xl, xr);
  if(yt > yb) std::swap(yt, yb);
  size_t p = 4;
  for(int y = yt; y <= yb && p < n; y++)
   for(int x = xl; x <= xr && p < n; x++, p++)
    if(x >= 0 && x < TEX_W && y >= 0 && y < TEX_H)
    {
     int o = y * TEX_W + x;
     const int bank = PE[0] & (TEX_BANKS - 1);
     Texture[bank][o] = body[p];
     TextureValid[bank][o] = 1;
    }
 }
 else if(option == 0 && n >= 5)
 {
  size_t p = 5; // z, xl, yt, xr, yb
  int xl = S16(body[1]), yt = S16(body[2]), xr = S16(body[3]), yb = S16(body[4]);
  if(xl > xr) std::swap(xl, xr);
  if(yt > yb) std::swap(yt, yb);
  for(int y = yt; y <= yb && p < n; y++)
   for(int x = xl; x <= xr && p < n; x++, p++)
    if(x >= 0 && x < FB_W && y >= 0 && y < FB_H)
    {
     int o = y * FB_W + x;
     FrameBuffer[DrawBuffer][o] = RGB444ToNative(body[p]);
     FrameValid[DrawBuffer][o] = 1;
    }
 }
 else if(option == 2 && n >= 4)
 {
  size_t p = 4;
  int xl = S16(body[0]), yt = S16(body[1]), xr = S16(body[2]), yb = S16(body[3]);
  if(xl > xr) std::swap(xl, xr);
  if(yt > yb) std::swap(yt, yb);
  for(int y = yt; y <= yb && p + 1 < n; y++)
   for(int x = xl; x <= xr && p + 1 < n; x++, p += 2)
    if(x >= 0 && x < FB_W && y >= 0 && y < FB_H)
    {
     int o = y * FB_W + x;
     FrameBuffer[DrawBuffer][o] = RGB444ToNative(body[p]);
     FrameValid[DrawBuffer][o] = 1;
    }
 }
 Complete(INT_PESYNC);
}

static void DoTriangleList(uint8 option, const uint16* body, size_t n)
{
 size_t p = 0;
 if(option == 0)
 {
  while(p + 12 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++) { uint16 c = body[p++]; ReadXYZ(body,n,p,v[i],c); }
   DrawTriangle(v[0],v[1],v[2],false);
  }
 }
 else if(option == 1)
 {
  while(p + 21 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++) { uint16 c = body[p++]; ReadXYZ(body,n,p,v[i],c); p += std::min<size_t>(3, n - p); }
   DrawTriangle(v[0],v[1],v[2],false);
  }
 }
 else if(option == 2)
 {
  while(p + 13 <= n)
  {
   Vtx v[3];
   ReadXYZ(body,n,p,v[0],0);
   ReadXYZ(body,n,p,v[1],0);
   uint16 c = body[p++];
   ReadXYZ(body,n,p,v[2],c);
   v[0].color = v[1].color = v[2].color = c;
   p += std::min<size_t>(3, n - p);
   DrawTriangle(v[0],v[1],v[2],false);
  }
 }
 else if(option == 4 || option == 5)
 {
  const bool has_norm = (option == 5);
  while(p + (has_norm ? 24 : 15) <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++)
   {
    uint16 u = body[p++], vv = body[p++];
    ReadXYZ(body,n,p,v[i],TE[90] ? TE[90] : 0x0FFF,u,vv);
    if(has_norm) p += std::min<size_t>(3, n - p);
   }
   DrawTriangle(v[0],v[1],v[2],true);
  }
 }
 else if(option == 6)
 {
  while(p + 18 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++)
   {
    uint16 u = body[p++], vv = body[p++];
    ReadXYZ(body,n,p,v[i],TE[90] ? TE[90] : 0x0FFF,u,vv);
   }
   p += std::min<size_t>(3, n - p);
   DrawTriangle(v[0],v[1],v[2],true);
  }
 }
 else if(option == 8 || option == 9)
 {
  const bool has_norm = (option == 9);
  while(p + (has_norm ? 18 : 9) <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++) { ReadXYZ(body,n,p,v[i],TE[90] ? TE[90] : 0x0FFF); if(has_norm) p += std::min<size_t>(3, n - p); }
   DrawTriangle(v[0],v[1],v[2],false);
  }
 }
 else if(option == 0xA)
 {
  while(p + 12 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++) ReadXYZ(body,n,p,v[i],TE[90] ? TE[90] : 0x0FFF);
   p += std::min<size_t>(3, n - p);
   DrawTriangle(v[0],v[1],v[2],false);
  }
 }
 Complete(INT_PESYNC);
}

static void DoTriangleStrip(uint8 option, const uint16* body, size_t n)
{
 std::vector<Vtx> verts;
 size_t p = 0;

 if(option == 6)
 {
  // Texture map + facet normal: first three vertices are followed by the
  // triangle normal, then each additional vertex is followed by the next
  // facet normal.  Vertex format is u,v,x,y,z.
  while(p + 5 <= n)
  {
   Vtx v;
   uint16 u = body[p++], vv = body[p++];
   ReadXYZ(body, n, p, v, TE[90] ? TE[90] : 0x0FFF, u, vv);
   verts.push_back(v);
   if(verts.size() >= 3)
   {
    DrawTriangle(verts[verts.size() - 3], verts[verts.size() - 2], verts[verts.size() - 1], true);
    p += std::min<size_t>(3, n - p);
   }
  }
  Complete(INT_PESYNC);
  return;
 }

 while(p < n)
 {
  Vtx v;
  if(option == 0)
  {
   if(p + 4 > n) break;
   uint16 c = body[p++];
   ReadXYZ(body,n,p,v,c);
  }
  else if(option == 4 || option == 5)
  {
   if(p + 5 > n) break;
   uint16 u = body[p++], vv = body[p++];
   ReadXYZ(body,n,p,v,TE[90] ? TE[90] : 0x0FFF,u,vv);
   if(option == 5) p += std::min<size_t>(3, n - p);
  }
  else
  {
   if(p + 3 > n) break;
   ReadXYZ(body,n,p,v,TE[90] ? TE[90] : 0x0FFF);
   if(option == 1 || option == 9) p += std::min<size_t>(3, n - p);
  }
  verts.push_back(v);
 }
 for(size_t i = 2; i < verts.size(); i++)
  DrawTriangle(verts[i-2], verts[i-1], verts[i], option == 4 || option == 5);
 Complete(INT_PESYNC);
}

static void DoLines(uint8 option, const uint16* body, size_t n, bool list)
{
 std::vector<Vtx> verts;
 size_t p = 0;
 while(p + 3 <= n)
 {
  Vtx v;
  uint16 c = TE[90] ? TE[90] : 0x0FFF;
  if(option == 0 && p + 4 <= n)
   c = body[p++];
  if(!ReadXYZ(body,n,p,v,c)) break;
  verts.push_back(v);
 }
 if(list)
 {
  for(size_t i = 1; i < verts.size(); i += 2)
   DrawLine(verts[i-1].sx, verts[i-1].sy, verts[i].sx, verts[i].sy, verts[i-1].color);
 }
 else
 {
  for(size_t i = 1; i < verts.size(); i++)
   DrawLine(verts[i-1].sx, verts[i-1].sy, verts[i].sx, verts[i].sy, verts[i-1].color);
 }
 Complete(INT_PESYNC);
}

static void DoMisc(uint8 option, const uint16* body, size_t n)
{
 if(option == 0 && n >= 6)
 {
  // Fill D/Z buffer: xleft, ytop, xright, ybottom, d, z.
  FillRect(S16(body[0]), S16(body[1]), S16(body[2]), S16(body[3]), RGB444ToNative(body[4]));
  Complete(INT_PESYNC);
  return;
 }

 if(option == 6 && n >= 16)
 {
  int16 cmd[16];
  for(int i = 0; i < 16; i++) cmd[i] = S16(body[i]);
  MatrixMul187(cmd, MatrixSrc4, MatrixDst4);
  Complete(INT_TESYNC);
  return;
 }

 if(option == 5 && n >= 9)
 {
  // 3x3 1.0.15 matrix op.  Same Game uses this primarily for the normal
  // matrix path; the current renderer does not light polygons accurately yet,
  // so preserve enough state for subsequent copies without perturbing object
  // transforms.
  for(int r = 0; r < 3; r++)
   for(int c = 0; c < 3; c++)
    MatrixDst4[r * 4 + c] = (int16)((S16(body[r * 3 + c]) + 0x40) >> 8);
  MatrixDst4[3] = MatrixDst4[7] = MatrixDst4[11] = 0;
  MatrixDst4[12] = MatrixDst4[13] = MatrixDst4[14] = 0;
  MatrixDst4[15] = 0x0080;
  Complete(INT_TESYNC);
  return;
 }

 if(option == 8)
 {
  memcpy(MatrixSrc4, MatrixDst4, sizeof(MatrixSrc4));
  CopyMatrixToTESource(MatrixSrc4);
  Complete(INT_TESYNC);
  return;
 }

 if(option == 9)
 {
  CopyMatrixToTEObject(MatrixDst4);
  Complete(INT_TESYNC);
  return;
 }

 if(option == 0xA)
 {
  const uint8 a[9]  = {56,58,60,62,64,66,68,70,72};
  for(int i = 0; i < 9; i++)
   TE[a[i]] = (uint16)MatrixDst4[(i / 3) * 4 + (i % 3)];
  Complete(INT_TESYNC);
  return;
 }

 if(option == 0xB)
 {
  for(int i = 0; i < 16; i++)
   Results[i] = (uint16)MatrixDst4[i];
  Complete(INT_TESYNC);
  return;
 }

 if(option == 0xC)
 {
  for(int i = 0; i < 6; i++)
   Results[i] = (uint16)TE[97 + i * 2];
  Complete(INT_TESYNC);
  return;
 }

 if(option == 2)
  Complete(INT_TESYNC);
 else
  Complete(INT_PESYNC | INT_TESYNC);
}

static void ProcessCommand(const uint16* cmd, size_t count)
{
 if(!count)
  return;
 if(cmd[0] == 0xBEEF)
  return;

 const uint16 h = cmd[0];
 const uint8 op = (h >> 12) & 0xF;
 const uint8 option = (h >> 8) & 0xF;
 const uint16* body = cmd + 1;
 const size_t n = count - 1;

#ifdef HUC6273_DEBUG
 static int cmdlog = 0;
 if((op != OP_NOP || option != 0) && cmdlog < 5000)
 {
  fprintf(stderr, "HUC_CMD op=%X opt=%X len=%zu head=%04x", op, option, count, h);
  size_t lim = n < 40 ? n : 40;
  for(size_t i = 0; i < lim; i++) fprintf(stderr, " %04x", body[i]);
  fprintf(stderr, "\n");
  cmdlog++;
 }
#endif

 switch(op)
 {
  case OP_NOP: Complete(); break;
  case OP_TRIANGLE_STRIP: DoTriangleStrip(option, body, n); break;
  case OP_TRIANGLE_LIST: DoTriangleList(option, body, n); break;
  case OP_POLY_LINE: DoLines(option, body, n, false); break;
  case OP_LINE_LIST: DoLines(option, body, n, true); break;
  case OP_PUT_IMAGE: DoPutImage(option, body, n); break;
  case OP_READ_PIXEL:
   if(n >= 2)
   {
    int x = S16(body[0]), y = S16(body[1]);
    ReadBack = (x >= 0 && x < FB_W && y >= 0 && y < FB_H && FrameValid[DrawBuffer][y * FB_W + x]) ? FrameBuffer[DrawBuffer][y * FB_W + x] : 0;
   }
   Complete(INT_RBDONE);
   break;
  case OP_WRITE_TE_REGISTERS: DoWriteTE(option, body, n); break;
  case OP_WRITE_PE_REGISTERS: DoWritePE(option, body, n); break;
  case OP_MISC: DoMisc(option, body, n); break;
  case OP_READ_TE_REGISTERS:
   ReadBack = n ? TE[body[0] & 0xFF] : 0;
   Complete(INT_RBDONE);
   break;
  case OP_READ_PE_REGISTERS:
   ReadBack = PE[option & 0xF];
   Complete(INT_RBDONE);
   break;
  case OP_WRITE_LUT:
   for(size_t i = 0; i + 1 < n; i += 2)
    LUT[body[i] & 0xFF] = body[i + 1];
   Complete(INT_PESYNC);
   break;
  case OP_READ_LUT:
   ReadBack = n ? LUT[body[0] & 0xFF] : 0;
   Complete(INT_RBDONE);
   break;
  default:
   Complete(INT_CSE);
   break;
 }
}

static void DrainFIFO(void)
{
 for(;;)
 {
  if(PendingFIFO.empty())
   return;
  if(PendingFIFO[0] == 0xBEEF)
  {
   PendingFIFO.erase(PendingFIFO.begin());
   continue;
  }
  const uint16 header = PendingFIFO[0];
  const uint8 op = (header >> 12) & 0xF;

  // NOP is only opcode 0/option 0 in the command set.  GMAKER texture streams
  // place raw pixel halfwords between 0x61FF PutImage chunks; these data words
  // often decode as opcode 0 with nonzero option/length.  Drop opcode-0 words
  // singly so the FIFO parser can resynchronize on the next real command
  // header instead of consuming a large fake NOP packet.
  if(op == OP_NOP)
  {
   PendingFIFO.erase(PendingFIFO.begin());
   Complete();
   continue;
  }

  uint8 length = header & 0xFF;
  if(length == 0)
   length = 1;
  if(PendingFIFO.size() < length)
   return;
  ProcessCommand(&PendingFIFO[0], length);
  PendingFIFO.erase(PendingFIFO.begin(), PendingFIFO.begin() + length);
 }
}

static void RunCMT(void)
{
 // CMTStartAddress is documented as an address, and the FIFO accepts halfwords.
 // Use byte-to-halfword conversion and wrap within the 128KiB command/texture window.
 uint32 start = ((uint32)CMTStartAddress >> 1) & 0xFFFF;
 uint32 count = ((uint32)CMTByteCount >> 1) & 0xFFFF;
 if(!count)
  return;
 uint32 pos = start;
 uint32 remaining = count;
 while(remaining)
 {
  uint16 h = CommandTextureMem[pos & 0xFFFF];
  if(h == 0xBEEF)
  {
   pos++;
   remaining--;
   continue;
  }
  uint8 len = h & 0xFF;
  if(len == 0) len = 1;
  if(len > remaining)
   break;
  uint16 tmp[0x100];
  for(uint8 i = 0; i < len; i++)
   tmp[i] = CommandTextureMem[(pos + i) & 0xFFFF];
  ProcessCommand(tmp, len);
  pos += len;
  remaining -= len;
 }
 Complete(INT_CMDONE);
}

uint8 HuC6273_Read8(uint32 A)
{
 uint16 v = HuC6273_Read16(A & ~1);
 return (uint8)(v >> ((A & 1) * 8));
}

uint16 HuC6273_Read16(uint32 A)
{
 A &= 0xFFFFF;
 if(A >= 0x10000 && A <= 0x2FFFF)
  return CommandTextureMem[((A - 0x10000) >> 1) & 0xFFFF];

 switch(A & ~1)
 {
  case 0x00000:
  case 0x00002: return FIFO_CAPACITY; // Free FIFO halfword slots.  We consume immediately.
  case 0x00004: return FIFOControl;
  case 0x00006: return CMTBankSelect;
  case 0x00008: return CMTStartAddress;
  case 0x0000A: return CMTByteCount;
  case 0x0000C: return InterruptMask;
  case 0x0000E: return 0;
  case 0x00010: return InterruptStatus;
  case 0x00012: return ReadBack;
  case 0x00014: return HorizontalTiming;
  case 0x00016: return VerticalTiming;
  case 0x00018: return SCTAddress;
  case 0x0001A: return SpriteControl;
  case 0x0001C: return CDResult[0];
  case 0x0001E: return CDResult[1];
  case 0x00020: return SPWindowX[0];
  case 0x00022: return SPWindowY[0];
  case 0x00024: return SPWindowX[1];
  case 0x00026: return SPWindowY[1];
  case 0x00028: return MiscStatus & ~((1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 11) | (1 << 12));
  case 0x0002A: return ErrorStatus;
  case 0x0002C: return DisplayControl;
  case 0x0002E: return (1 << 10) | (2 << 8) | 1; // width=12-bit-ish, 32-bank texture, rev 1
  case 0x00030: return TECodeControl;
  case 0x00032: return TEAddressControl;
  case 0x0003C: return RasterHit;
  case 0x00040: return PixelEngineTest;
  case 0x00042: return MemoryTest;
 }
 if(A >= 0x00060 && A <= 0x0007E)
  return Results[(A >> 1) & 0xF];
 return 0;
}

void HuC6273_Write16(uint32 A, uint16 V)
{
 A &= 0xFFFFF;
 if(A >= 0x10000 && A <= 0x2FFFF)
 {
  CommandTextureMem[((A - 0x10000) >> 1) & 0xFFFF] = V;
  return;
 }

 switch(A & ~1)
 {
  case 0x00000:
  case 0x00002:
   if(PendingFIFO.size() < 0x200)
    PendingFIFO.push_back(V);
   else
    InterruptStatus |= INT_OVF;
   DrainFIFO();
   break;
  case 0x00004: FIFOControl = V; break;
  case 0x00006: CMTBankSelect = V & 0x1F; break;
  case 0x00008: CMTStartAddress = V & 0xFFFE; break;
  case 0x0000A:
   CMTByteCount = V & 0xFFFE;
   RunCMT();
   break;
  case 0x0000C: InterruptMask = V; break;
  case 0x0000E: InterruptStatus &= ~V; break;
  case 0x00010: InterruptStatus = V; break;
  case 0x00012: ReadBack = V; break;
  case 0x00014: HorizontalTiming = V; break;
  case 0x00016: VerticalTiming = V; break;
  case 0x00018: SCTAddress = V; break;
  case 0x0001A: SpriteControl = V; Complete(INT_SPDONE); break;
  case 0x0001C: CDResult[0] = V; break;
  case 0x0001E: CDResult[1] = V; break;
  case 0x00020: SPWindowX[0] = V; break;
  case 0x00022: SPWindowY[0] = V; break;
  case 0x00024: SPWindowX[1] = V; break;
  case 0x00026: SPWindowY[1] = V; break;
  case 0x00028: MiscStatus = V; break;
  case 0x0002C: DisplayControl = V; break;
  case 0x0002E:
   StatusControl = V;
   if(V & 0x3)
   {
    PendingFIFO.clear();
    InterruptStatus = INT_AEMP;
    MiscStatus = 0;
    ErrorStatus = 0x0011;
   }
   break;
  case 0x00030: TECodeControl = V; break;
  case 0x00032: TEAddressControl = V; break;
  case 0x0003C: RasterHit = V; break;
  case 0x00040: PixelEngineTest = V; break;
  case 0x00042: MemoryTest = V; break;
  default:
   if(A >= 0x00060 && A <= 0x0007E)
    Results[(A >> 1) & 0xF] = V;
   break;
 }
}

void HuC6273_Write32(uint32 A, uint32 V)
{
 A &= 0xFFFFF;

 // The FXGA FIFO and command/texture window are documented as word/halfword
 // accessible.  GMAKER emits command tables as 32-bit constants with the
 // command header in the upper halfword (for example 0x61ff0802).  The Aurora
 // side consumes that upper halfword first.  Do not use the generic V810
 // little-endian split order for this bus.
 const uint16 hi = (uint16)(V >> 16);
 const uint16 lo = (uint16)V;

 if(A == 0x00000 || A == 0x00002)
 {
  HuC6273_Write16(A, hi);
  HuC6273_Write16(A, lo);
  return;
 }

 if(A >= 0x10000 && A <= 0x2FFFF)
 {
  CommandTextureMem[((A - 0x10000) >> 1) & 0xFFFF] = hi;
  CommandTextureMem[(((A - 0x10000) >> 1) + 1) & 0xFFFF] = lo;
  return;
 }

 HuC6273_Write16(A, lo);
 HuC6273_Write16(A + 2, hi);
}

void HuC6273_Write8(uint32 A, uint8 V)
{
 uint16 old = HuC6273_Read16(A & ~1);
 if(A & 1)
  old = (old & 0x00FF) | ((uint16)V << 8);
 else
  old = (old & 0xFF00) | V;
 HuC6273_Write16(A & ~1, old);
}

void HuC6273_Reset(void)
{
 FIFOControl = 0x0205;
 CMTBankSelect = 0;
 CMTStartAddress = 0;
 CMTByteCount = 0;
 InterruptMask = 0;
 InterruptStatus = INT_AEMP | INT_VBL | INT_VSY;
 ReadBack = 0;
 HorizontalTiming = VerticalTiming = 0;
 SCTAddress = SpriteControl = 0;
 CDResult[0] = CDResult[1] = 0;
 SPWindowX[0] = SPWindowY[0] = 0;
 SPWindowX[1] = FB_W - 1;
 SPWindowY[1] = FB_H - 1;
 MiscStatus = 0;
 ErrorStatus = 0x0011;
 DisplayControl = StatusControl = RasterHit = 0;
 TECodeControl = TEAddressControl = 0;
 PixelEngineTest = MemoryTest = 0;
 memset(Results, 0, sizeof(Results));
 memset(TE, 0, sizeof(TE));
 memset(PE, 0, sizeof(PE));
 memset(LUT, 0, sizeof(LUT));
 memset(CommandTextureMem, 0, sizeof(CommandTextureMem));
 memset(FrameBuffer, 0, sizeof(FrameBuffer));
 memset(FrameValid, 0, sizeof(FrameValid));
 memset(Texture, 0, sizeof(Texture));
 memset(TextureValid, 0, sizeof(TextureValid));
 ClearZ();
 MatrixIdentity(MatrixSrc4);
 MatrixIdentity(MatrixDst4);
 ObjectMatrixValid = false;
 FrontBuffer = 0;
 DrawBuffer = 0;
 TE[2] = TE[20] = TE[38] = 0x0080; // object matrix identity, 1.8.7
 TE[75] = 0x0080; TE[77] = 0x0080; TE[79] = 0xFF80; TE[81] = 0x0080;
 TE[90] = 0x0FFF;
 PendingFIFO.clear();
}

bool HuC6273_Init(void)
{
 HuC6273_Reset();
 return TRUE;
}

void HuC6273_RenderLine(uint16* target, int y, int width)
{
 if(y < 0 || y >= FB_H || !target)
  return;
 int w = std::min(width, FB_W);
 const uint16* src = FrameBuffer[FrontBuffer] + y * FB_W;
 const uint8* valid = FrameValid[FrontBuffer] + y * FB_W;
 for(int x = 0; x < w; x++)
  if(valid[x])
   target[x] = src[x];
}
