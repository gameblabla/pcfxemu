/* Mednafen - Multi-system Emulator
 *
 * This file contains a conservative first-pass HuC6273(Aurora) implementation
 * for PC-FXGA software.  It is not a cycle-accurate renderer.  The goal here
 * is to model the command/register interface well enough for FARL/GMAKER titles
 * to leave their hardware probes and submit/display simple fixed-point geometry.
 */

#include "pcfx.h"
#include "huc6273.h"
#include <mednafen/state_helpers.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int g_trace_frame = 0;
static FILE* g_trace_fp = NULL;
static int g_trace_from = -1, g_trace_to = -1;
static inline FILE* TraceFP(void)
{
#ifdef PCFX_WASM
 return NULL; /* Browser builds have no process environment/file-based trace. */
#else
 if(g_trace_from < 0) { const char* p = getenv("HUC_LOG"); if(!p) { g_trace_from = 1<<30; return NULL; } g_trace_fp = fopen(p, "w"); g_trace_from = getenv("HUC_LOG_FROM") ? atoi(getenv("HUC_LOG_FROM")) : 0; g_trace_to = getenv("HUC_LOG_TO") ? atoi(getenv("HUC_LOG_TO")) : (1<<30); }
 if(!g_trace_fp || g_trace_frame < g_trace_from || g_trace_frame > g_trace_to) return NULL;
 return g_trace_fp;
#endif
}

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

enum { FB_W = 256, FB_H = 240, TEX_W = 256, TEX_H = 256 };

// FARL/HuC6273 control bits used by the first-pass renderer.
enum { TE_CTRL_ICM = 1 << 0, TE_CTRL_NNF = 1 << 1, TE_CTRL_BCE = 1 << 2, TE_CTRL_LTEN = 1 << 5, TE_CTRL_SPEN = 1 << 4, TE_CTRL_RJM = 1 << 9, PE_CTRL_C12M = 1 << 10, PE_CTRL_ZCAT = 1 << 9, PE_CTRL_OLEN = 1 << 8, PE_CTRL_TLEN = 1 << 5, PE_CTRL_TLM = 1 << 4 };


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

// 0x80510000..0x8051FFFF is the packed/CMT window.  FARL's unpacked
// texture XY writes use the following 128KiB region as 256x256 9-bit texels.
static uint16 CommandTextureMem[0x10000];

static MDFN_Pixel FrameBuffer[3][FB_W * FB_H];
static uint8 FrameValid[3][FB_W * FB_H];

#define HUC6273_PIX_INVALID 0
#define HUC6273_PIX_CLEAR   1
#define HUC6273_PIX_DRAWN   2
static int32 ZBuffer[FB_W * FB_H];
enum { TEX_BANKS = 32 };
static uint16 Texture[TEX_BANKS][TEX_W * TEX_H];
static uint8 TextureValid[TEX_BANKS][TEX_W * TEX_H];
static uint16 TextureUnpacked[TEX_BANKS][TEX_W * TEX_H];
static uint8 TextureUnpackedValid[TEX_BANKS][TEX_W * TEX_H];
static int FrontBuffer;
static int DrawBuffer;
static int PendingBuffer;
static bool SwapPending;
// The pending swap waits for Vblank (PE frame control SWV=1).
static bool SwapPendingVsync;
// NEC C6273_A (Appendix A): from a swap command until the swap is done, the
// memory controller holds Busy.  Only the PE frame control register and
// buffer memory accesses wait on it, so a dummy swap or a drawing command
// issued in that period stalls the PE until the swap.  While stalled, PE sync
// completions are held here and posted when the swap happens (MSLSync and
// NEC's "swap, dummy swap, PE sync, wait" sequence rely on this).
static bool PEStalled;
static uint16 HeldPEBits;
static bool PELockedUntilFrame;

// ---------------------------------------------------------------------------
// Command timing.  NEC C6273_1 sec. 1.1.1 gives the drawing rates: about
// 100,000 triangles per second (a triangle strip transforms one new vertex per
// triangle; a list transforms three and is slower, sec. 1.2.8), about 5 M
// pixels per second for Z-compared or texture-mapped 3D pixels, and about
// 10 M pixels per second for 3D pixels without Z compare and for 2D pixels.
// The emulator still draws every command at once.  What is timed is when the
// hardware would report the work done (TE sync, PE sync, read back done, FIFO
// almost empty, sprite done, command macro done, the busy bits in Misc
// status) and when a Vblank swap can happen.  Software that waits on those,
// such as GMAKER's AGL reading matrix results after a TE sync or MSLSync
// waiting for the swap, then runs at the hardware's pace.
//
// Times are V810 cycles (21.47727 MHz) since power on.  Choices NEC does not
// state: the TE (vertices) and the PE (pixels) work in parallel on a command,
// a command's pixels cannot finish before its vertices, matrix operations and
// register writes cost nothing, and the buffer clear after a swap (Display
// control BCM) runs at the 2D pixel rate.
//
// Lit vertices (TE control LTEN, primitives with normals) are slower than
// NEC's maximum rate, by an amount NEC does not give.  HUC_CYC_PER_LIT_VERTEX
// is fitted to videos of two programs on a real PC-FXGA.  BULLY OFF: the
// title loop runs every 2nd Vblank and all three race rounds every 4th; with
// NEC's 215 cycles round 3 runs partly every 3rd Vblank.  Same Game FX: its
// title intro (letter grid to "PUSH RUN BUTTON") takes 20.0 s; 300 cycles
// stretch it to 21.4 s, 270 to 20.6 s.  Of the values measured, 225 to 260
// cycles reproduce all of these; 240 (about 11 us, 89,500 lit vertices per
// second) is inside that range.
// ---------------------------------------------------------------------------
enum { HUC_CYC_PER_VERTEX = 215,              // 21.47727 MHz / 100,000
       HUC_CYC_PER_LIT_VERTEX = 240 };        // fitted, see above
#define HUC_CYC_PER_PX_SLOW_X1000 4295         // 21.47727 MHz / 5 M, x1000
#define HUC_CYC_PER_PX_FAST_X1000 2148         // 21.47727 MHz / 10 M, x1000
static uint64 TimeBase;       // cycles before the current emulated frame
static uint32 TimeNowTS;      // current V810 timestamp within the frame
static uint64 TEFree, PEFree, SPFree;
static uint64 SwapReadyTime;
static uint64 CntVerts, CntPxSlow, CntPxFast;
static bool InCommand;
static uint16 CmdBits;
enum { TIMED_BITS = 6, TIMED_QLEN = 1024 };
static const uint16 TimedBit[TIMED_BITS] = { INT_AEMP, INT_PESYNC, INT_TESYNC, INT_RBDONE, INT_SPDONE, INT_CMDONE };
static uint64 TimedQ[TIMED_BITS][TIMED_QLEN];
static uint32 TimedHead[TIMED_BITS], TimedCount[TIMED_BITS];

// Command FIFO fill (NEC C6273_4 2.3.3, 2.3.4 note 1): 32 words deep; the
// status register reads back the number of free words (AFW).  A command's
// words are counted as in the FIFO until the TE has taken them: all of them
// before the TE starts on it, then in proportion until it is done.  Software
// such as FARL's FarlWriteFifoH checks AFW before each write, so it waits
// while the TE is behind.  (Words of a command still being written are not
// counted.)
enum { FIFO_DEPTH = 32, FIFOQ_LEN = 64 };
static uint64 FifoQStart[FIFOQ_LEN], FifoQEnd[FIFOQ_LEN];
static uint32 FifoQWords[FIFOQ_LEN];
static uint32 FifoQHead, FifoQCount;

static inline uint64 HucNow(void) { return TimeBase + TimeNowTS; }
static inline uint64 HucMax(uint64 a, uint64 b) { return a > b ? a : b; }
static unsigned PELockFramesRemaining;
static bool DeferredPEReadPending;
static uint8 DeferredPEReadReg;
static uint16 LastSpriteSCTAddress;
static uint16 LastSpriteControl;
static bool LastSpriteValid;
static bool ReplayingSprites;

static uint16 PendingFIFO[0x200];
static uint32 PendingFIFOCountLive;

static inline int huc_min_int(int a, int b) { return a < b ? a : b; }
static inline int huc_max_int(int a, int b) { return a > b ? a : b; }
static inline void huc_swap_int(int *a, int *b) { int t = *a; *a = *b; *b = t; }

static inline bool PendingFIFOEmpty(void) { return PendingFIFOCountLive == 0; }
static inline void PendingFIFOPopFrontN(uint32 count)
{
 if(count >= PendingFIFOCountLive)
 {
  PendingFIFOCountLive = 0;
  return;
 }
 memmove(PendingFIFO, PendingFIFO + count, (PendingFIFOCountLive - count) * sizeof(PendingFIFO[0]));
 PendingFIFOCountLive -= count;
}
static inline void PendingFIFOPush(uint16 v)
{
 if(PendingFIFOCountLive < (uint32)(sizeof(PendingFIFO) / sizeof(PendingFIFO[0])))
  PendingFIFO[PendingFIFOCountLive++] = v;
 else
  InterruptStatus |= INT_OVF;
}

// Post every timed completion that is due by 'now'.
static void TimedPost(uint64 now)
{
 for(int b = 0; b < TIMED_BITS; b++)
 {
  while(TimedCount[b] && TimedQ[b][TimedHead[b]] <= now)
  {
   InterruptStatus |= TimedBit[b];
   TimedHead[b] = (TimedHead[b] + 1) % TIMED_QLEN;
   TimedCount[b]--;
  }
 }
}

// Post 'bits' at time 'due'.  Bits without timing are posted at once.  Each
// unit finishes its work in order, so each queue stays in time order.
static void TimedSchedule(uint16 bits, uint64 due, uint64 now)
{
 for(int b = 0; b < TIMED_BITS; b++)
 {
  if(!(bits & TimedBit[b]))
   continue;
  bits &= ~TimedBit[b];
  if(TimedCount[b])
  {
   const uint32 last = (TimedHead[b] + TimedCount[b] - 1) % TIMED_QLEN;
   if(due < TimedQ[b][last])
    due = TimedQ[b][last];
   if(TimedCount[b] == TIMED_QLEN)
   {
    TimedQ[b][last] = due;
    continue;
   }
  }
  else if(due <= now)
  {
   InterruptStatus |= TimedBit[b];
   continue;
  }
  TimedQ[b][(TimedHead[b] + TimedCount[b]) % TIMED_QLEN] = due;
  TimedCount[b]++;
 }
 InterruptStatus |= bits;
}

static uint32 FifoAvailable(uint64 now)
{
 while(FifoQCount && FifoQEnd[FifoQHead] <= now)
 {
  FifoQHead = (FifoQHead + 1) % FIFOQ_LEN;
  FifoQCount--;
 }
 uint64 used = 0;
 for(uint32 i = 0; i < FifoQCount; i++)
 {
  const uint32 k = (FifoQHead + i) % FIFOQ_LEN;
  if(now < FifoQStart[k] || FifoQEnd[k] <= FifoQStart[k])
   used += FifoQWords[k];
  else
   used += (FifoQWords[k] * (FifoQEnd[k] - now) + (FifoQEnd[k] - FifoQStart[k]) - 1) / (FifoQEnd[k] - FifoQStart[k]);
  if(used >= FIFO_DEPTH)
   return 0;
 }
 return FIFO_DEPTH - (uint32)used;
}

static void FifoQPush(uint64 start, uint64 end, uint32 words)
{
 if(FifoQCount == FIFOQ_LEN)
 {
  FifoQHead = (FifoQHead + 1) % FIFOQ_LEN;
  FifoQCount--;
 }
 const uint32 k = (FifoQHead + FifoQCount) % FIFOQ_LEN;
 FifoQStart[k] = start;
 FifoQEnd[k] = end;
 FifoQWords[k] = words;
 FifoQCount++;
}

static inline uint64 PixelCycles(uint64 slow, uint64 fast)
{
 return (slow * HUC_CYC_PER_PX_SLOW_X1000 + fast * HUC_CYC_PER_PX_FAST_X1000) / 1000;
}

// Reset or soft restart: every unit goes idle and pending completions go.
static void TimingIdle(void)
{
 const uint64 now = HucNow();
 TEFree = PEFree = SPFree = now;
 SwapReadyTime = 0;
 memset(TimedHead, 0, sizeof(TimedHead));
 memset(TimedCount, 0, sizeof(TimedCount));
 FifoQHead = FifoQCount = 0;
 InCommand = false;
 CmdBits = 0;
}

void HuC6273_SetTime(uint32 ts)
{
 TimeNowTS = ts;
}

void HuC6273_EndFrame(uint32 ts_end)
{
 TimeBase += ts_end;
 TimeNowTS = 0;
}


extern MDFN_Pixel FXVCE_GetPaletteNative(uint16 index);
static MDFN_Pixel RGB444ToNative(uint16 c);
static MDFN_Pixel TextureWordToNative(uint16 pix, float shade);
static MDFN_Pixel TextureIndex9ToNative(uint16 pix, float shade);
static MDFN_Pixel ColorWordToNative(uint16 pix);
static void Complete(uint16 bits);

static inline int16 S16(uint16 v) { return (int16)v; }
static inline uint16 Clamp16(int v) { return (uint16)(v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : v)); }

static void MatrixIdentity(int16 m[16])
{
 memset(m, 0, sizeof(int16) * 16);
 m[0] = m[5] = m[10] = m[15] = 0x0080;
}

// HuC6273 matrix unit, per NEC "HuC6273 device manual" (GMAKER DOC/DEVICE
// C6273_5, sec. 3.6.10.4-3.6.10.17):
//  - 16-bit multiplier, 32-bit accumulator.
//  - 1.0.15 ops: the low 15 bits of the accumulated sum are truncated.
//  - 1.8.7 ops: the low 7 fraction bits and the top 8 integer bits are
//    truncated, with no overflow detection (the result wraps to 16 bits).
//  - Mdst = Mcmd . Msrc for 4x4 and 3x3 ops (command matrix on the left).
//  - 3x3 ops occupy the upper-left 3x3 of the 4x4 register grid (FARL reads
//    them back from result regs 0,1,2,4,5,6,8,9,10).
static void MatrixOpNEC(const int16 cmd[16], const int16 src[16], int16 dst[16], int dim, int shift)
{
 int16 r[16];
 memcpy(r, dst, sizeof(r));
 for(int row = 0; row < dim; row++)
  for(int col = 0; col < dim; col++)
  {
   int32 v = 0;
   for(int k = 0; k < dim; k++)
    v += (int32)cmd[row * 4 + k] * (int32)src[k * 4 + col];
   r[row * 4 + col] = (int16)(uint16)(uint32)(v >> shift);
  }
 memcpy(dst, r, sizeof(r));
}

// 3x1 op (1.0.15): Mdst[3] = Msrc[3][3] . Mcmd[3].  The vector lives in
// column 0 of the register grid (FARL reads it back from result regs 0,4,8).
static void MatrixVecOpNEC(const int16 vec[3], const int16 src[16], int16 dst[16])
{
 for(int row = 0; row < 3; row++)
 {
  int32 v = 0;
  for(int k = 0; k < 3; k++)
   v += (int32)src[row * 4 + k] * (int32)vec[k];
  dst[row * 4] = (int16)(uint16)(uint32)(v >> 15);
 }
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

static int32 Matrix187Raw(uint8 addr, int32 def)
{
 if(!ObjectMatrixValid)
  return def;
 return (int32)S16(TE[addr]);
}

static void ClearZTo(int32 z)
{
 for(int i = 0; i < FB_W * FB_H; i++)
  ZBuffer[i] = z;
}

static void ClearZ(void)
{
 ClearZTo(-0x7FFFFFFF);
}

static void ClearAuroraFrameStorage(bool reset_buffer_indices)
{
 memset(FrameBuffer, 0, sizeof(FrameBuffer));
 memset(FrameValid, 0, sizeof(FrameValid));
 ClearZ();
 if(reset_buffer_indices)
 {
  FrontBuffer = 0;
  DrawBuffer = 1;
  PendingBuffer = 2;
  SwapPending = false;
  SwapPendingVsync = false;
  PEStalled = false;
  HeldPEBits = 0;
  PELockedUntilFrame = false;
  PELockFramesRemaining = 0;
  DeferredPEReadPending = false;
  LastSpriteValid = false;
  ReplayingSprites = false;
 }
}

static int CountClearTextureCoverage(int bank, int xoff, int yoff)
{
 bank &= (TEX_BANKS - 1);
 int covered = 0;
 for(int y = 0; y < FB_H; y++)
 {
  const int sy = (y + yoff) & (TEX_H - 1);
  const int row = sy * TEX_W;
  for(int x = 0; x < FB_W; x++)
  {
   const int sx = (x + xoff) & (TEX_W - 1);
   if(TextureUnpackedValid[bank][row + sx] || TextureValid[bank][row + sx])
    covered++;
  }
 }
 return covered;
}

static bool SelectClearTextureBank(int* out_bank, int* out_coverage, int xoff, int yoff)
{
 if(!out_bank || !out_coverage)
  return false;

 // PE register 9 is the documented clear-texture selector.  A few early
 // FARL/GMAKER samples also leave useful clear texture data in the current PE
 // texture bank or bank 0, so probe those as conservative fallbacks.
 const int candidates[3] = { PE[9] & (TEX_BANKS - 1), PE[0] & (TEX_BANKS - 1), 0 };
 int best_bank = candidates[0];
 int best_coverage = -1;
 for(size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
 {
  bool seen = false;
  for(size_t j = 0; j < i; j++)
   if(candidates[i] == candidates[j])
    seen = true;
  if(seen)
   continue;
  const int coverage = CountClearTextureCoverage(candidates[i], xoff, yoff);
  if(coverage > best_coverage)
  {
   best_bank = candidates[i];
   best_coverage = coverage;
  }
 }

 *out_bank = best_bank;
 *out_coverage = best_coverage < 0 ? 0 : best_coverage;
 return best_coverage > 0;
}

static bool ClearDrawBufferFromClearTexture(uint16 clear_word)
{
 const int xoff = PE[11] & (TEX_W - 1);
 const int yoff = PE[12] & (TEX_H - 1);
 int bank = 0;
 int coverage = 0;
 if(!SelectClearTextureBank(&bank, &coverage, xoff, yoff))
  return false;

 // Maze2D/ASL keeps buffer-clear mode enabled while a small AID sprite sheet is
 // resident in texture memory; blindly copying that sheet caused the raw 64x64
 // art to appear in the next frame.  N-nyuu's title clear texture is a real
 // screen/background clear source and covers most of the 256x240 Aurora field.
 // Require broad coverage before accepting the texture clear path; otherwise
 // fall back to the D clear colour or to an invalid/transparent Aurora frame.
 if(coverage < (FB_W * FB_H * 3) / 4)
  return false;

 const int clear_bank = bank & (TEX_BANKS - 1);
 const bool have_clear_colour = (clear_word & 0x0FFF) != 0;
 const MDFN_Pixel clear_native = have_clear_colour ? ColorWordToNative(clear_word) : 0;
 for(int y = 0; y < FB_H; y++)
 {
  const int sy = (y + yoff) & (TEX_H - 1);
  for(int x = 0; x < FB_W; x++)
  {
   const int sx = (x + xoff) & (TEX_W - 1);
   const int so = sy * TEX_W + sx;
   const int doff = y * FB_W + x;
   if(TextureUnpackedValid[clear_bank][so])
   {
    FrameBuffer[DrawBuffer][doff] = TextureIndex9ToNative(TextureUnpacked[clear_bank][so], 1.0f);
    FrameValid[DrawBuffer][doff] = HUC6273_PIX_DRAWN;
   }
   else if(TextureValid[clear_bank][so])
   {
    FrameBuffer[DrawBuffer][doff] = TextureWordToNative(Texture[clear_bank][so], 1.0f);
    FrameValid[DrawBuffer][doff] = HUC6273_PIX_DRAWN;
   }
   else if(have_clear_colour)
   {
    FrameBuffer[DrawBuffer][doff] = clear_native;
    FrameValid[DrawBuffer][doff] = HUC6273_PIX_DRAWN;
   }
   else
   {
    FrameBuffer[DrawBuffer][doff] = 0;
    FrameValid[DrawBuffer][doff] = 0;
   }
  }
 }
 return true;
}

static void ClearDrawBufferForNextFrame(void)
{
 // DisplayControl bits 9..10 select explicit HuC6273 buffer-clear modes.
 // Bit 7 is only the hidden-clear enable gate; Same Game FX leaves this bit
 // set while drawing its tunnel/title scene, but does not request a texture
 // clear.  Treating bit 7 alone as a CWT clear copied the alphabet texture
 // sheet over Same Game's background.
 const uint16 clear_mode = (DisplayControl >> 9) & 0x3;
 const bool hidden_clear_enabled = (DisplayControl & (1 << 7)) != 0;
 const uint16 clear_word = PE[6] & 0x0FFF;

 // PE[9]/PE[11]/PE[12] are the clear texture/CWT source registers.  Use
 // them only when the program selected a texture-capable buffer-clear mode;
 // mode 1 is a plain D/Z clear used by GMAKER sprite samples, and hidden clear
 // by itself is not sufficient evidence that the texture buffer should become
 // the next frame background.
 if(clear_mode >= 2 && ClearDrawBufferFromClearTexture(clear_word))
 {
  // Texture-backed clear already populated validity and pixels.
 }
 else if(clear_mode || (hidden_clear_enabled && clear_word))
 {
  // An explicit buffer-clear mode clears the next draw buffer to the PE clear
  // colour even when that colour is zero.  The previous code treated zero as
  // transparent/invalid, exposing the underlying KING/VDC layer; GMAKER's
  // sprite sample therefore showed a grey PC-FX background instead of the
  // HuC6273's black cleared D buffer.  Keep hidden-clear-only zero as
  // transparent to avoid reintroducing the Same Game alphabet-sheet/overlay
  // regressions fixed earlier.
  const MDFN_Pixel c = clear_word ? ColorWordToNative(PE[6]) : RGB444ToNative(0);
  for(int i = 0; i < FB_W * FB_H; i++)
  {
   FrameBuffer[DrawBuffer][i] = c;
   FrameValid[DrawBuffer][i] = (clear_word ? HUC6273_PIX_DRAWN : HUC6273_PIX_CLEAR);
  }
 }
 else
 {
  memset(FrameBuffer[DrawBuffer], 0, sizeof(FrameBuffer[DrawBuffer]));
  memset(FrameValid[DrawBuffer], 0, sizeof(FrameValid[DrawBuffer]));
 }
 ClearZ();
}

static unsigned BufferSwapLockFrames(uint16 frame_control)
{
 // FARL/ASL code commonly performs a VBlank swap followed by a dummy PE read
 // to wait for the previous swap to become visible.  This wait is frame-
 // boundary latched, not a multi-frame hidden-clear stall.  The older v8/v10
 // model added a broad +9-frame delay whenever display-control clear bits were
 // set; Maze2D leaves those bits enabled during normal play, which throttled
 // the entire game.  Keep the conservative one-frame PE readback latency that
 // fixes mid-scan/half-sprite presentation without globally slowing ASL titles.
 (void)frame_control;
 return 1;
}

static void ReplayLastSprites(void);

static void RequestBufferSwap(uint16 frame_control)
{
 // ASL/GMAKER often submits sprite commands before AGLShot() kicks the
 // buffered 3D command list.  Replay the last sprite run at swap time so
 // HUD/announcement sprites land above the just-finished Aurora geometry.
 // (An emulator aid, so its pixels are not counted as drawing time.)
 {
  const uint64 slow = CntPxSlow, fast = CntPxFast;
  ReplayLastSprites();
  CntPxSlow = slow;
  CntPxFast = fast;
 }
 // HuC6273 buffer swaps are frame-latched.  Copy the completed draw buffer
 // into a pending display buffer immediately so software can start rendering
 // the next frame without the visible frame changing mid-scanline.
 memcpy(FrameBuffer[PendingBuffer], FrameBuffer[DrawBuffer], sizeof(FrameBuffer[DrawBuffer]));
 memcpy(FrameValid[PendingBuffer], FrameValid[DrawBuffer], sizeof(FrameValid[DrawBuffer]));
 ClearDrawBufferForNextFrame();
 SwapPending = true;
 SwapPendingVsync = (frame_control & 0x0008) != 0;
 // The swap waits for the drawing issued before it.
 SwapReadyTime = HucMax(HucMax(TEFree, PEFree), HucNow());
 PELockedUntilFrame = true;
 PELockFramesRemaining = BufferSwapLockFrames(frame_control);
}

static void ApplyPendingSwap(uint64 now)
{
 if(!SwapPending)
  return;
 // A Vblank swap happens at the first Vblank after the drawing issued before
 // it is done.
 if(SwapPendingVsync && SwapReadyTime > now)
  return;

 const int old_front = FrontBuffer;
 FrontBuffer = PendingBuffer;
 PendingBuffer = old_front;
 SwapPending = false;
 SwapPendingVsync = false;
 // The buffer clear after the swap (Display control BCM) keeps the memory
 // busy before the PE can go on.  Hidden clear (HCE) does not change this
 // clear (NEC C6273_4 2.3.13, C6273_A Appendix B note 1).
 uint64 resume = HucMax(PEFree, now);
 if((DisplayControl >> 9) & 3)
  resume += PixelCycles(0, (uint64)FB_W * FB_H);
 PEFree = resume;
 // The swap is done: Busy drops and a stalled PE carries on.
 if(PEStalled)
 {
  PEStalled = false;
  TimedSchedule(HeldPEBits, PEFree, now);
  HeldPEBits = 0;
 }
}

static void AdvancePELockFrame(void)
{
 if(SwapPending)
  return;  // still waiting for the swap
 if(PELockFramesRemaining)
 {
  PELockFramesRemaining--;
  if(!PELockFramesRemaining)
   PELockedUntilFrame = false;
 }
 else if(!SwapPending)
  PELockedUntilFrame = false;
}

static void CompleteDeferredPERead(void)
{
 if(DeferredPEReadPending && !PELockedUntilFrame)
 {
  ReadBack = PE[DeferredPEReadReg & 0xF];
  DeferredPEReadPending = false;
  Complete(INT_RBDONE);
 }
}

void HuC6273_FrameBoundary(void)
{
 { FILE* fp = TraceFP(); if(fp) fprintf(fp, "FRAME %d\n", g_trace_frame); }
 g_trace_frame++;
 const uint64 now = HucNow();
 TimedPost(now);
 ApplyPendingSwap(now);
 AdvancePELockFrame();
 InterruptStatus |= INT_FSY | INT_VSY | INT_VBL;
 MiscStatus |= (1 << 9) | (1 << 10);
 CompleteDeferredPERead();
}

static MDFN_Pixel RGB444ToNative(uint16 c)
{
 // FARL examples commonly use 12-bit I/C or RGB-ish constants.  Same Game's
 // early setup uses an IC mask of 0x0FFF; interpreting this as RGB444 gives a
 // useful, deterministic first approximation for flat primitive color.
 uint8 r = ((c >> 8) & 0xF) * 17;
 uint8 g = ((c >> 4) & 0xF) * 17;
 uint8 b = ((c >> 0) & 0xF) * 17;
 return (MDFN_Pixel)MAKECOLOR(r, g, b, 0xFF);
}

static MDFN_Pixel BGR444ToNative(uint16 c)
{
 // Aurora C12M sprite examples from GMAKER store 12-bit source words with the
 // red component in the low nibble.  Keep the older RGB helper for primitive
 // fallback colours, but use this channel order for those direct ASL sprites.
 uint8 r = ((c >> 0) & 0xF) * 17;
 uint8 g = ((c >> 4) & 0xF) * 17;
 uint8 b = ((c >> 8) & 0xF) * 17;
 return (MDFN_Pixel)MAKECOLOR(r, g, b, 0xFF);
}

static MDFN_Pixel BGR444ToNativeExpanded8(uint16 c)
{
 // Some GMAKER C12M ASL samples, including psxdemo's ball, use a compact
 // 0..8 intensity ramp in one BGR444 nibble.  On the hardware/S-Video capture
 // that ramp expands to the visible 12-bit display range; treating it as a
 // literal 0..15 nibble leaves only the bright center visible and makes the
 // ball look like a crooked sliver.  Expand this specific 8-level source form
 // before converting to the frontend native pixel format.
 int r = ((c >> 0) & 0xF) * 2;
 int g = ((c >> 4) & 0xF) * 2;
 int b = ((c >> 8) & 0xF) * 2;
 if(r > 15) r = 15;
 if(g > 15) g = 15;
 if(b > 15) b = 15;
 return BGR444ToNative((uint16)((b << 8) | (g << 4) | r));
}

static inline int32 Fixed115ToInt(uint16 v)
{
 return (int32)S16(v);
}

typedef struct Vtx
{
 // Clip-space position: [x' y' z' W] = Mobj . [x y z 1] (NEC HuC6273 manual
 // 1.2.3; column-vector convention, perspective term in object_matrix[3][2]).
 float cx, cy, cz, cw;
 // Device coordinates (window scale applied) and normalised screen Z (z'/W).
 float fsx, fsy;
 int sx, sy;
 float zs;
 int32 zi;
 float raw_x, raw_y, raw_z;
 uint16 color;
 float u, v;
 float shade;
 float nx, ny, nz;
 float src_nx, src_ny, src_nz;
 bool normal_valid;
 bool src_normal_valid;
} Vtx;

typedef enum
{
 CULL_PROJECTED_AREA,
 CULL_TRANSFORMED_NORMAL,
} CullMode;

static int TEWin(uint8 addr, int def)
{
 uint16 v = TE[addr];
 return v ? (int)S16(v) : def;
}

// Window scale (NEC 3.6.8.8): xd = scx * xs + trx, yd = scy * ys + try.
static void VtxFinishProjection(Vtx* v)
{
 const int sx_scale = TEWin(75, 128);
 const int sx_trans = TEWin(77, 128);
 const int sy_scale = TEWin(79, -128);
 const int sy_trans = TEWin(81, 128);
 const float w = v->cw;
 const float xs = v->cx / w;
 const float ys = v->cy / w;
 v->zs = v->cz / w;
 v->fsx = xs * sx_scale + sx_trans;
 v->fsy = ys * sy_scale + sy_trans;
 v->sx = (int)lrintf(v->fsx);
 v->sy = (int)lrintf(v->fsy);
 v->zi = (int32)lrintf(v->zs * 32768.0f);
}

static void ProjectVertex(Vtx* out, uint16 xw, uint16 yw, uint16 zw, uint16 color, uint16 u, uint16 v)
{
 CntVerts++;
 const int32 ivx = S16(xw), ivy = S16(yw), ivz = S16(zw);
 out->raw_x = (float)ivx / 32768.0f;
 out->raw_y = (float)ivy / 32768.0f;
 out->raw_z = (float)ivz / 32768.0f;

 // Transform (NEC C6273_2 sec. 1.2.3 and 1.2.6): the object matrix is 1.8.7
 // and the vertex 1.0.15.  NEC does not give the format of the result
 // x', y', z', W.  It is taken here as 1.8.7, the matrix format: each sum of
 // products is rounded (half adds 1, then floor) and its low 15 bits
 // dropped.  The sum is kept in 64 bits; an overflow of the chip's own
 // accumulator, or a wrap of the result at 16 bits, is not modelled.  Before
 // any object matrix is loaded the identity is used, so those vertices are
 // also cut to 7 fraction bits.
 // The precision matters where surfaces lie very close together.  On
 // BULLY OFF's first opponent car (the canopy over the fin root, the wheel
 // hubs inside the body, the nose) it decides which surface passes the Z
 // compare.  At 1.8.7 the car shows marks the PC-FXGA video shows during the
 // round 1 approach (dark slits and outlines on the front wheels, the hub
 // rings on the middle of the body, a dark hole in the nose); in full float
 // precision none of these appear.  The matrix unit's own ops truncate;
 // truncating here too puts the rear wheel's hub in front of its tire in
 // side views, which the video does not show, so this rounds.
 // Object matrix register layout from FARL.H: rows are not contiguous.
 const uint8 maddr[4][4] = { { 2, 4, 12, 14 }, { 16, 20, 22, 30 }, { 32, 34, 38, 40 }, { 48, 50, 52, 54 } };
 float clip[4];
 for(int r = 0; r < 4; r++)
 {
  const int64 acc = (int64)Matrix187Raw(maddr[r][0], r == 0 ? 0x80 : 0) * ivx +
                    (int64)Matrix187Raw(maddr[r][1], r == 1 ? 0x80 : 0) * ivy +
                    (int64)Matrix187Raw(maddr[r][2], r == 2 ? 0x80 : 0) * ivz +
                    (int64)Matrix187Raw(maddr[r][3], r == 3 ? 0x80 : 0) * 32768;
  clip[r] = (float)((acc + (1 << 14)) >> 15) / 128.0f;
 }
 out->cx = clip[0];
 out->cy = clip[1];
 out->cz = clip[2];
 out->cw = clip[3];
 if(out->cw > 0.0f)
  VtxFinishProjection(out);
 else
 {
  out->fsx = out->fsy = 0.0f;
  out->sx = out->sy = 0;
  out->zs = 0.0f;
  out->zi = 0;
 }

 out->color = color;
 out->u = (float)u;
 out->v = (float)v;
 out->shade = 1.0f;
 out->nx = out->ny = out->nz = 0.0f;
 out->src_nx = out->src_ny = out->src_nz = 0.0f;
 out->normal_valid = false;
 out->src_normal_valid = false;
}

static bool ReadXYZ(const uint16* cmd, size_t count, size_t* pos, Vtx* out, uint16 color, uint16 u, uint16 v)
{
 if(*pos + 3 > count)
  return false;
 ProjectVertex(out, cmd[*pos], cmd[*pos + 1], cmd[*pos + 2], color, u, v);
 *pos += 3;
 return true;
}

static void FillRect(int xl, int yt, int xr, int yb, MDFN_Pixel native_color, int32 z_value)
{
 if(xl > xr) huc_swap_int(&xl, &xr);
 if(yt > yb) huc_swap_int(&yt, &yb);
 xl = huc_max_int(0, huc_min_int(FB_W - 1, xl));
 xr = huc_max_int(0, huc_min_int(FB_W - 1, xr));
 yt = huc_max_int(0, huc_min_int(FB_H - 1, yt));
 yb = huc_max_int(0, huc_min_int(FB_H - 1, yb));
 CntPxFast += (uint64)(xr - xl + 1) * (uint64)(yb - yt + 1);
 for(int y = yt; y <= yb; y++)
  for(int x = xl; x <= xr; x++)
  {
   const int o = y * FB_W + x;
   FrameBuffer[DrawBuffer][o] = native_color;
   FrameValid[DrawBuffer][o] = HUC6273_PIX_DRAWN;
   ZBuffer[o] = z_value;
  }
}

static inline int Edge(int ax, int ay, int bx, int by, int cx, int cy)
{
 return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

static inline int64 Edge64(int ax, int ay, int bx, int by, int cx, int cy)
{
 return (int64)(cx - ax) * (int64)(by - ay) - (int64)(cy - ay) * (int64)(bx - ax);
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

static bool FARLDefaultI5C4Word(uint16 pix, uint16 mask)
{
 return ((mask & 0x0FFF) == 0x0F8F) && (pix & 0x1000);
}

static uint16 FARLDefaultI5C4Index(uint16 pix)
{
 // FARL/GMAKER default-colour register values in I5C4 mode are byte-style
 // intensity/colour selectors, not PE-mask-packed 12-bit source pixels.
 // The hardware path resolves those selectors directly to the I5C4 VCE row;
 // compressing them through PE[7] drops the high intensity bit and makes
 // default-coloured HuC6273 meshes too dark and hue-shifted.
 return (uint16)((((pix >> 4) & 0x01F0) | ((pix - 1) & 0x000F)) & 0x01FF);
}


static uint16 FARLDefaultI5C4IndexShaded(uint16 pix, float shade)
{
 int intensity = (pix >> 8) & 0x1F;
 const int color = pix & 0x0F;
 if(shade < 0.0f) shade = 0.0f;
 if(shade > 1.5f) shade = 1.5f;
 float mat_shade = shade;
 if(mat_shade < 1.0f)
  mat_shade = mat_shade * mat_shade * 0.82f;
 intensity = (int)lrintf((float)intensity * mat_shade);
 if(intensity < 0) intensity = 0;
 if(intensity > 0x1F) intensity = 0x1F;
 return (uint16)(((intensity << 4) | color) & 0x01FF);
}

static int ICIntensityBits(uint16 mask)
{
 switch(mask & 0x0FFF)
 {
  case 0x0FC7: return 6;
  case 0x0F8F: return 5;
  case 0x0F1F: return 4;
  case 0x0E3F: return 3;
  default:     return 0;
 }
}

static uint16 ApplyICShade(uint16 pix, float shade)
{
 if(shade < 0.0f) shade = 0.0f;
 if(shade > 2.0f) shade = 2.0f;

 const uint16 mask = PE[7] ? (PE[7] & 0x0FFF) : 0x0FC7;
 const int ibits = ICIntensityBits(mask);
 if(!ibits)
  return pix & 0x0FFF;

 const int ishift = 12 - ibits;
 const int imax = (1 << ibits) - 1;
 int intensity = (pix >> ishift) & imax;

 // The HuC6273 I/C path appears to have more visible low-end contrast than a
 // straight multiply when viewed against Same Game FX hardware capture: cyan
 // and blue cube side faces should drop clearly while front faces stay bright.
 // Keep values >= 1.0 linear, but bend sub-unity lighting darker.
 float ic_shade = shade;
 if(ic_shade < 1.0f)
  ic_shade = ic_shade * (0.78f + 0.22f * ic_shade);

 intensity = (int)lrintf((float)intensity * ic_shade);
 if(intensity < 0) intensity = 0;
 if(intensity > imax) intensity = imax;
 return (uint16)((pix & ~(((uint16)imax) << ishift)) | (intensity << ishift));
}

static MDFN_Pixel ShadeNative(MDFN_Pixel c, float shade)
{
 if(shade < 0.0f) shade = 0.0f;
 if(shade > 1.5f) shade = 1.5f;
#if defined(WANT_16BPP)
 const int rmax = (int)(RED_MASK >> RED_SHIFT);
 const int gmax = (int)(GREEN_MASK >> GREEN_SHIFT);
 const int bmax = (int)(BLUE_MASK >> BLUE_SHIFT);
 int r = (int)(((c & RED_MASK) >> RED_SHIFT) * 255 + (rmax >> 1)) / rmax;
 int g = (int)(((c & GREEN_MASK) >> GREEN_SHIFT) * 255 + (gmax >> 1)) / gmax;
 int b = (int)(((c & BLUE_MASK) >> BLUE_SHIFT) * 255 + (bmax >> 1)) / bmax;
#else
 int r = (int)((c >> RED_SHIFT) & 0xFF);
 int g = (int)((c >> GREEN_SHIFT) & 0xFF);
 int b = (int)((c >> BLUE_SHIFT) & 0xFF);
#endif
 r = (int)lrintf((float)r * shade);
 g = (int)lrintf((float)g * shade);
 b = (int)lrintf((float)b * shade);
 if(r > 255) r = 255;
 if(g > 255) g = 255;
 if(b > 255) b = 255;
 return (MDFN_Pixel)MAKECOLOR(r, g, b, 0xFF);
}


static MDFN_Pixel ApplyNeutralPostPaletteShade(MDFN_Pixel native, float shade)
{
 // Same Game's cursor/hand model uses a very light neutral material.  Applying
 // light only before palette compression leaves those pixels clustered at white
 // on the current approximation, losing the grey Gouraud/facet falloff visible
 // in the S-Video hardware capture.  Keep saturated cube colors on the normal
 // I/C path, but compress light neutral material after palette lookup too.
#if defined(WANT_16BPP)
 const int rmax = (int)(RED_MASK >> RED_SHIFT);
 const int gmax = (int)(GREEN_MASK >> GREEN_SHIFT);
 const int bmax = (int)(BLUE_MASK >> BLUE_SHIFT);
 int r8 = (int)(((native & RED_MASK) >> RED_SHIFT) * 255 + (rmax >> 1)) / rmax;
 int g8 = (int)(((native & GREEN_MASK) >> GREEN_SHIFT) * 255 + (gmax >> 1)) / gmax;
 int b8 = (int)(((native & BLUE_MASK) >> BLUE_SHIFT) * 255 + (bmax >> 1)) / bmax;
#else
 int r8 = (int)((native >> RED_SHIFT) & 0xFF);
 int g8 = (int)((native >> GREEN_SHIFT) & 0xFF);
 int b8 = (int)((native >> BLUE_SHIFT) & 0xFF);
#endif
 int mx = huc_max_int(r8, huc_max_int(g8, b8));
 int mn = huc_min_int(r8, huc_min_int(g8, b8));
 int luma = (77 * r8 + 150 * g8 + 29 * b8) >> 8;
 int sat = mx - mn;
 if(luma < 150 || sat > 58)
  return native;
 float s = shade;
 if(s > 1.12f) s = 1.12f;
 if(s < 0.25f) s = 0.25f;
 // Keep the light neutral hand material bright while preserving the per-facet
 // Gouraud/facet falloff.  The previous post-palette curve was too aggressive
 // and made Same Game's cursor hand look uniformly dull.
 s = 0.15f + 0.85f * s;
 if(s > 1.04f) s = 1.04f;
 return ShadeNative(native, s);
}

static MDFN_Pixel RGB444ToNativeShaded(uint16 c, float shade)
{
 if(shade < 0.0f) shade = 0.0f;
 if(shade > 2.0f) shade = 2.0f;
 int r = (int)lrintf(((c >> 8) & 0xF) * shade);
 int g = (int)lrintf(((c >> 4) & 0xF) * shade);
 int b = (int)lrintf(((c >> 0) & 0xF) * shade);
 if(r > 15) r = 15;
 if(g > 15) g = 15;
 if(b > 15) b = 15;
 return RGB444ToNative((uint16)((r << 8) | (g << 4) | b));
}

static MDFN_Pixel ColorWordToNativeShaded(uint16 pix, float shade)
{
 if((TE[255] & TE_CTRL_ICM) && !(PE[3] & PE_CTRL_C12M))
 {
  const uint16 mask = PE[7] ? (PE[7] & 0x0FFF) : 0x0FC7;
  if(TE[90] == 0x0801 && pix == TE[90])
   // FARL/GMAKER default-colour 0x0801 is an I5C4 material selector.  Apply
   // facet lighting by moving along the authored I5C4 intensity ramp, preserving
   // the PC-FXGA palette hue used by Bully Off's vehicle body panels.
   return FXVCE_GetPaletteNative(FARLDefaultI5C4IndexShaded(pix, shade));
  if(FARLDefaultI5C4Word(pix, mask))
   return FXVCE_GetPaletteNative(FARLDefaultI5C4Index(pix));
  return ApplyNeutralPostPaletteShade(FXVCE_GetPaletteNative(CompressIC(ApplyICShade(pix, shade), mask)), shade);
 }
 return ApplyNeutralPostPaletteShade(RGB444ToNativeShaded(pix, shade), shade);
}

// I-C colour (NEC 1.3.1): the 12-bit colour word holds the I field in bits
// 11..6 and the C field in bits 5..0.  PE I-C mask selects which bits survive
// packing to the 9-bit texel / HuC6261 palette index (CompressIC), and 9-bit
// texels are unpacked back into those bit positions (ExpandIC).
static uint16 ExpandIC(uint16 packed, uint16 mask)
{
 uint16 out = 0;
 int ib = 0;
 for(int b = 0; b < 12; b++)
  if(mask & (1U << b))
  {
   if(packed & (1U << ib))
    out |= (1U << b);
   ib++;
  }
 return out;
}

static inline uint16 ICMask(void)
{
 return PE[7] ? (PE[7] & 0x0FFF) : 0x0FC7;
}

// Scale the I field of a 12-bit I-C word and look the packed result up in the
// HuC6261 palette.  The scaled intensity is formed at the precision the PE
// I-C mask keeps (e.g. 5 bits in I5C4) and saturates at that field's maximum.
static int ICIntensityBitsOfMask(uint16 mask)
{
 int n = 0;
 for(int b = 6; b < 12; b++)
  if(mask & (1U << b))
   n++;
 return n;
}

// The scaled intensity is truncated.  NEC does not document the rounding.
// Truncation is what matches the PC-FXGA captures of BULLY OFF once each
// capture's level curve is matched through the unlit background picture:
// the floor's mean brightness comes out within 1 (of 255) of both the
// video and approximate_bullyoff_256x240.png, where rounding to nearest
// is 8 brighter; over whole frames of the round 1 approach the difference
// is under 2 with truncation and 2 to 6 with rounding.  (v1.0 chose
// rounding from a comparison that scaled the levels but missed the
// captures' raised black level.)
static int ScaleIntensity(int i_src, float factor, int imax)
{
 const float fi = (float)i_src * (factor < 0.0f ? 0.0f : factor);
 const int i = (int)fi;
 return i > imax ? imax : i;
}

static MDFN_Pixel ICWordLitToNative(uint16 w12, float factor)
{
 const uint16 mask = ICMask();
 w12 &= 0x0FFF;
 if(factor != 1.0f)
 {
  const int ib = ICIntensityBitsOfMask(mask);
  const int sh = 12 - ib;
  const int imax = (1 << ib) - 1;
  const int i = ScaleIntensity((w12 >> sh) & imax, factor, imax);
  w12 = (uint16)((w12 & ~(imax << sh)) | (i << sh));
 }
 return FXVCE_GetPaletteNative(CompressIC(w12, mask));
}

// Untextured I-C primitive: the lit 6-bit I (vertex I x lighting factor,
// interpolated) is truncated, then packed through the I-C mask like any
// colour word, which keeps the mask's top I bits.
static MDFN_Pixel ICLitColorToNative(uint16 c_word, float lit_i6)
{
 int i6 = (int)(lit_i6 < 0.0f ? 0.0f : lit_i6);
 if(i6 > 0x3F)
  i6 = 0x3F;
 return ICWordLitToNative((uint16)((c_word & 0x3F) | (i6 << 6)), 1.0f);
}

static float Fix115ToFloat(uint16 v)
{
 return (float)S16(v) / 32768.0f;
}

static void Normalize3(float* x, float* y, float* z)
{
 const float l = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
 if(l > 1.0e-9f)
 {
  *x /= l; *y /= l; *z /= l;
 }
}

static void DecodeSourceNormal(uint16 nxw, uint16 nyw, uint16 nzw, float* nx, float* ny, float* nz)
{
 *nx = Fix115ToFloat(nxw);
 *ny = Fix115ToFloat(nyw);
 *nz = Fix115ToFloat(nzw);
 Normalize3(nx, ny, nz);
}

static float LightDot(uint8 ax, uint8 ay, uint8 az, float nx, float ny, float nz)
{
 float lx = Fix115ToFloat(TE[ax]);
 float ly = Fix115ToFloat(TE[ay]);
 float lz = Fix115ToFloat(TE[az]);
 Normalize3(&lx, &ly, &lz);
 // L points from the surface toward the light: NEC's AGL light test
 // (GMAKER SAMPLE/AGL/LTEST.C) draws L as a line from the origin, and the
 // surfaces facing the end of that line are the lit ones.
 const float d = nx * lx + ny * ly + nz * lz;
 return d > 0.0f ? d : 0.0f;
}

static void TransformNormal(uint16 nxw, uint16 nyw, uint16 nzw, float* tx, float* ty, float* tz)
{
 float nx, ny, nz;
 DecodeSourceNormal(nxw, nyw, nzw, &nx, &ny, &nz);

 // Transform primitive normals by the TE normal matrix (NEC 3.6.8.2: the
 // normal matrix and the primitive normals are both 1.0.15).  The result is
 // renormalised below, so only the direction is used.
 const float m00 = Fix115ToFloat(TE[56]), m01 = Fix115ToFloat(TE[58]), m02 = Fix115ToFloat(TE[60]);
 const float m10 = Fix115ToFloat(TE[62]), m11 = Fix115ToFloat(TE[64]), m12 = Fix115ToFloat(TE[66]);
 const float m20 = Fix115ToFloat(TE[68]), m21 = Fix115ToFloat(TE[70]), m22 = Fix115ToFloat(TE[72]);

 *tx = m00 * nx + m01 * ny + m02 * nz;
 *ty = m10 * nx + m11 * ny + m12 * nz;
 *tz = m20 * nx + m21 * ny + m22 * nz;
 Normalize3(tx, ty, tz);
}

// Lighting (NEC 1.2.5): ambient light plus two directional lights, each
// coefficient being light intensity x surface reflectance (TE light
// coefficient register, 0.0.15).  The manual names the terms but does not give
// the combining formula; this uses the conventional form
//   factor = ambient + diffuse1 * max(0, N.L1) + diffuse2 * max(0, N.L2)
// which multiplies the I (intensity) component of the primitive colour or of
// the texel (PE control TLEN=1, TLM=0), plus the specular terms below when
// TE control SPEN=1.  N is the primitive normal after the TE normal matrix; L
// is the TE light vector, already in view coordinates.
// Specular: NEC documents the two specular coefficients and the SPEN enable
// but not the term itself.  Blinn half-vector with exponent 8 is an estimate:
// of 4/8/16/32 it best matches the highlight distribution of BULLY OFF's car
// in the PC-FXGA S-Video capture (lum p50/p75/p90 99/124/141 vs 96/119/140).
static float SpecularTerm(uint8 ax, uint8 ay, uint8 az, float nx, float ny, float nz)
{
 const int expo = 8;
 float lx = Fix115ToFloat(TE[ax]);
 float ly = Fix115ToFloat(TE[ay]);
 float lz = Fix115ToFloat(TE[az]);
 Normalize3(&lx, &ly, &lz);
 // Half vector between the light and the viewer (view direction +z).
 float hx = lx, hy = ly, hz = lz + 1.0f;
 Normalize3(&hx, &hy, &hz);
 float d = nx * hx + ny * hy + nz * hz;
 if(d <= 0.0f)
  return 0.0f;
 return powf(d, (float)expo);
}

static float ComputeShadeFromNormal(float nx, float ny, float nz, bool textured)
{
 (void)textured;
 if(!(TE[255] & TE_CTRL_LTEN) || !(TE[255] & TE_CTRL_ICM))
  return 1.0f;

 const float ambient = (float)(TE[55] & 0x7FFF) / 32768.0f;
 const float diff1   = (float)(TE[57] & 0x7FFF) / 32768.0f;
 const float diff2   = (float)(TE[59] & 0x7FFF) / 32768.0f;
 const float spec1   = (float)(TE[61] & 0x7FFF) / 32768.0f;
 const float spec2   = (float)(TE[63] & 0x7FFF) / 32768.0f;
 float f = ambient;
 f += diff1 * LightDot(74, 76, 78, nx, ny, nz);
 f += diff2 * LightDot(80, 82, 84, nx, ny, nz);
 if(TE[255] & TE_CTRL_SPEN)
 {
  f += spec1 * SpecularTerm(74, 76, 78, nx, ny, nz);
  f += spec2 * SpecularTerm(80, 82, 84, nx, ny, nz);
 }
 return f;
}

static bool ReadNormalShadeEx(const uint16* cmd, size_t count, size_t* pos, float* shade, float* nx, float* ny, float* nz, float* src_nx, float* src_ny, float* src_nz, bool textured)
{
 if(*pos + 3 > count)
  return false;
 DecodeSourceNormal(cmd[*pos], cmd[*pos + 1], cmd[*pos + 2], src_nx, src_ny, src_nz);
 TransformNormal(cmd[*pos], cmd[*pos + 1], cmd[*pos + 2], nx, ny, nz);
 *shade = ComputeShadeFromNormal(*nx, *ny, *nz, textured);
 *pos += 3;
 return true;
}

static bool ReadNormalShade(const uint16* cmd, size_t count, size_t* pos, float* shade, float* nx, float* ny, float* nz, bool textured)
{
 float src_nx = 0.0f, src_ny = 0.0f, src_nz = 0.0f;
 return ReadNormalShadeEx(cmd, count, pos, shade, nx, ny, nz, &src_nx, &src_ny, &src_nz, textured);
}

static bool ReadVertexNormalShade(const uint16* cmd, size_t count, size_t* pos, Vtx* v, bool textured)
{
 if(!ReadNormalShadeEx(cmd, count, pos, &v->shade, &v->nx, &v->ny, &v->nz, &v->src_nx, &v->src_ny, &v->src_nz, textured))
  return false;
 v->normal_valid = true;
 v->src_normal_valid = true;
 return true;
}

static void ApplyFacetNormalEx(Vtx* v, float shade, float nx, float ny, float nz, float src_nx, float src_ny, float src_nz)
{
 v->shade = shade;
 v->nx = nx;
 v->ny = ny;
 v->nz = nz;
 v->src_nx = src_nx;
 v->src_ny = src_ny;
 v->src_nz = src_nz;
 v->normal_valid = true;
 v->src_normal_valid = true;
}

static void ApplyFacetNormal(Vtx* v, float shade, float nx, float ny, float nz)
{
 ApplyFacetNormalEx(v, shade, nx, ny, nz, nx, ny, nz);
 v->src_normal_valid = false;
}

static uint16 InterpWord(uint16 a, uint16 b, uint16 c, float fa, float fb, float fc)
{
 int v = (int)lrintf((float)(a & 0x1FFF) * fa + (float)(b & 0x1FFF) * fb + (float)(c & 0x1FFF) * fc);
 if(v < 0) v = 0;
 if(v > 0x1FFF) v = 0x1FFF;
 return (uint16)v;
}

static MDFN_Pixel ColorWordToNative(uint16 pix)
{
 // I-C mode (NEC 1.3.1): a 12-bit colour word (bits 15..12 ignored, the
 // register is 12 bits wide) packed through the PE I-C mask to a 9-bit HuC6261
 // palette index.
 if((TE[255] & TE_CTRL_ICM) && !(PE[3] & PE_CTRL_C12M))
  return FXVCE_GetPaletteNative(CompressIC(pix & 0x0FFF, ICMask()));
 return RGB444ToNative(pix);
}

static MDFN_Pixel TextureWordToNative(uint16 pix, float shade)
{
 // In normal FARL use, texture pixels are 9-bit I/C values embedded in a
 // 12-bit word according to PE IC-mask.  Lighting modulates the I component
 // before the I/C value is compressed to a VCE palette index.
 if(!(PE[3] & PE_CTRL_C12M))
 {
  const uint16 mask = PE[7] ? (PE[7] & 0x0FFF) : 0x0FC7;
  return ApplyNeutralPostPaletteShade(FXVCE_GetPaletteNative(CompressIC(ApplyICShade(pix, shade), mask)), shade);
 }
 return ApplyNeutralPostPaletteShade(RGB444ToNativeShaded(pix, shade), shade);
}

static uint16 ApplyI5C4IndexShade(uint16 idx, float shade)
{
 idx &= 0x01FF;
 if(shade < 0.0f) shade = 0.0f;
 if(shade > 2.0f) shade = 2.0f;
 int intensity = (idx >> 4) & 0x1F;
 const int color = idx & 0x0F;
 float ic_shade = shade;
 if(ic_shade < 1.0f)
  ic_shade = ic_shade * ic_shade * 0.82f;
 intensity = (int)lrintf((float)intensity * ic_shade);
 if(intensity < 0) intensity = 0;
 if(intensity > 0x1F) intensity = 0x1F;
 return (uint16)(((intensity << 4) | color) & 0x01FF);
}

static MDFN_Pixel TextureIndex9ToNative(uint16 pix, float shade)
{
 // FarlWriteTxtXYUnpk() stores an already-compressed 9-bit VCE texel.  The
 // unpacked AID path is not a single format: entries in the high I5C4 bank are
 // material ramp entries and TLEN walks their five intensity bits before VCE
 // lookup, while low-bank decal/highlight entries are direct VCE indices and
 // must be attenuated after lookup.  Reinterpreting low-bank entries as I5C4
 // intensity values made Bully Off's body highlights muddy and over-dark.
 uint16 idx = pix & 0x01FF;
 if((TE[255] & TE_CTRL_ICM) && !(PE[3] & PE_CTRL_C12M) && (PE[3] & PE_CTRL_TLEN) && (idx & 0x0100))
  return FXVCE_GetPaletteNative(ApplyI5C4IndexShade(idx, shade));
 return ShadeNative(FXVCE_GetPaletteNative(idx), shade);
}

static bool TextureWordIsTransparent(uint16 pix, bool zero_keyed)
{
 // Primitive overlay textures use palette/direct colour zero as the transparent
 // key.  Some playfield texture paths use zero as an opaque texel, so the
 // rasterizer passes zero_keyed=false only for those explicit playfield draws.
 if(!zero_keyed)
  return false;

 if(PE[3] & PE_CTRL_C12M)
  return ((pix & 0x0FFF) == 0);

 const uint16 mask = PE[7] ? (PE[7] & 0x0FFF) : 0x0FC7;
 return CompressIC(pix, mask) == 0;
}

static bool TextureIndex9IsTransparent(uint16 pix, bool zero_keyed)
{
 // AID INDEX9 sources produced by the SDK can mark unused unpacked texels with
 // bit 15 set.  The colour payload remains in the low 9 bits.
 return (pix & 0x8000) || (zero_keyed && ((pix & 0x01FF) == 0));
}


static bool UseUnpackedICWordTexel(int bank, uint16 pix)
{
 bank &= (TEX_BANKS - 1);

 // SDK AID INDEX9 planes use bit 15 as transparency and the low 9 bits as an
 // already-compressed VCE palette index.  Do not route those through the FARL
 // I/C compressor.
 if(pix & 0x8000)
  return false;

 // In direct 12-bit colour mode the source word is already colour data, not an
 // I/C pair that should be decoded through PE[7].
 if(PE[3] & PE_CTRL_C12M)
  return false;

 // If high I/C bits are actually present, this unpacked plane is a 12-bit
 // FARL texture-word plane even though it arrived through the unpacked writer.
 if(pix & 0x7E00)
  return true;

 const uint16 dc = TE[90] ? TE[90] : 0x0FFF;

 // FARL object textures that are authored as low-valued I/C material words can
 // also occupy the unpacked plane.  Distinguish that material state from GMAKER
 // floor/car AID planes, which are direct 9-bit palette-index art.
 if(bank == 1 && dc == 0x0C01 && (TE[255] & TE_CTRL_ICM))
  return true;

 return false;
}

static bool FetchTextureSample(int bank, uint32 o, bool unpacked_9bit, float shade, MDFN_Pixel* out, bool zero_keyed)
{
 bank &= (TEX_BANKS - 1);
 o &= 0xFFFF;
 if(unpacked_9bit)
 {
  if(!TextureUnpackedValid[bank][o])
   return false;
  const uint16 pix = TextureUnpacked[bank][o];
  if(TextureIndex9IsTransparent(pix, zero_keyed))
   return false;
  if(UseUnpackedICWordTexel(bank, pix))
   *out = TextureWordToNative(pix, shade);
  else
   *out = TextureIndex9ToNative(pix, shade);
 }
 else
 {
  if(!TextureValid[bank][o])
   return false;
  const uint16 pix = Texture[bank][o];
  if(TextureWordIsTransparent(pix, zero_keyed))
   return false;
  *out = TextureWordToNative(pix, shade);
 }
 return true;
}

static bool SampleTexture(uint16 u, uint16 v, uint16 fallback_word, float shade, MDFN_Pixel* out, bool zero_keyed)
{
 const int tx = (int)((u + TE[86]) & (TEX_W - 1));
 const int ty = (int)((v + TE[88]) & (TEX_H - 1));
 const uint32 o = (uint32)(ty * TEX_W + tx);
 const int bank = PE[0] & (TEX_BANKS - 1);

 // FarlWriteTxtXYUnpk() stores a full 256x256 9-bit texel plane.  FARL/GMAKER
 // can keep command/sprite tables in the packed plane of the same bank, so
 // unpacked texture data wins when both planes contain data at the same XY.
 if(FetchTextureSample(bank, o, true, shade, out, zero_keyed))
  return true;
 if(FetchTextureSample(bank, o, false, shade, out, zero_keyed))
  return true;

 // Texture RAM is still readable even where the emulator has not observed a
 // write.  Treat unwritten texels as zero instead of substituting the current
 // primitive colour.
 (void)fallback_word;
 if(TextureIndex9IsTransparent(0, zero_keyed))
  return false;
 *out = TextureIndex9ToNative(0, shade);
 return true;
}





static inline int SignExtendBits(uint16 v, int bits)
{
 const int m = 1 << (bits - 1);
 return (int)((v ^ m) - m);
}

static inline int SpriteDim(uint8 v)
{
 return (int)v + 1;
}

static uint16 ReadTexBankWord(int bank, uint32 hwaddr)
{
 bank &= (TEX_BANKS - 1);
 return Texture[bank][hwaddr & 0xFFFF];
}

static uint16 ReadTexBankUnpackedWord(int bank, uint32 hwaddr)
{
 bank &= (TEX_BANKS - 1);
 return TextureUnpacked[bank][hwaddr & 0xFFFF];
}

static void WriteTexBankWord(int bank, uint32 hwaddr, uint16 v)
{
 bank &= (TEX_BANKS - 1);
 const uint32 o = hwaddr & 0xFFFF;
 Texture[bank][o] = v;
 TextureValid[bank][o] = 1;
 if(bank == 0)
  CommandTextureMem[o] = v;
}

static void WriteTexBankUnpackedWord(int bank, uint32 hwaddr, uint16 v)
{
 bank &= (TEX_BANKS - 1);
 const uint32 o = hwaddr & 0xFFFF;
 TextureUnpacked[bank][o] = v;
 TextureUnpackedValid[bank][o] = 1;
}

static uint16 ReadSpriteSourceWord(int bank, int sx, int sy)
{
 const int sb = bank & (TEX_BANKS - 1);
 sx &= 0xFF;
 sy &= 0xFF;
 const uint32 src_addr0 = (((uint32)sy << 8) | (uint32)sx) & 0xFFFF;

 // GMAKER's SetTexture()/FarlWriteTxtXYUnpk() stores source art as a 9-bit
 // 256x256 plane; ASL sprite control tables can live in the packed plane of
 // the same bank.  Keep those planes separate so sprite rows past y=127 do
 // not alias CMT/font data.
 if(TextureUnpackedValid[sb][src_addr0])
  return TextureUnpacked[sb][src_addr0] & 0x01FF;
 if(TextureValid[sb][src_addr0])
  return Texture[sb][src_addr0] & 0x0FFF;
 return 0;
}

static bool SpriteSourceLooks8LevelBGR(int bank, int sx0, int sy0, int sw, int sh)
{
 int max_nibble = 0;
 if(sw <= 0 || sh <= 0 || sw > 256 || sh > 256)
  return false;

 for(int y = 0; y < sh; y++)
  for(int x = 0; x < sw; x++)
  {
   const uint16 p = ReadSpriteSourceWord(bank, sx0 + x, sy0 + y);
   if(p & 0x0FF0)
    return false;
   const int n = p & 0xF;
   if(n > max_nibble)
    max_nibble = n;
  }

 return max_nibble > 1 && max_nibble <= 8;
}

static MDFN_Pixel SpritePixelToNative(uint16 pix, bool direct_c12m_bgr, bool expand_8level_bgr)
{
 if(direct_c12m_bgr)
  return expand_8level_bgr ? BGR444ToNativeExpanded8(pix & 0x0FFF) : BGR444ToNative(pix & 0x0FFF);

 // ASL/GMAKER sprite sheets follow the same I/C path as texture primitives
 // when TE I-C mode is enabled.  The sprite_pcfxga sample stores source words
 // such as 0x0807 that must be compressed through PE's I/C mask before VCE
 // lookup; using pix & 0x1ff turned its white face tile into palette index 7
 // and produced a black block.  Keep direct VCE-indexed fallback for older AID
 // samples that do not enable I-C mode.
 if((TE[255] & TE_CTRL_ICM) && !(PE[3] & PE_CTRL_C12M))
 {
  const uint16 mask = PE[7] ? (PE[7] & 0x0FFF) : 0x0FC7;
  if((pix & 0x0100) && (mask & 0x0FFF) == 0x0F8F)
   return FXVCE_GetPaletteNative(pix & 0x01FF);
  if(FARLDefaultI5C4Word(pix, mask))
   return FXVCE_GetPaletteNative(FARLDefaultI5C4Index(pix));
  return FXVCE_GetPaletteNative(CompressIC(pix, mask));
 }
 return FXVCE_GetPaletteNative(pix & 0x1FF);
}

static void DrawSpriteEntry(int sct_bank, int sct_no)
{
 const uint32 base = ((uint32)(sct_no & 0x7FF) * 16) & 0xFFFF;
 uint16 sct[16];
 for(int i = 0; i < 16; i++)
  sct[i] = ReadTexBankWord(sct_bank, base + i);

 const uint16 head = sct[0];
 const int fnc = (head >> 13) & 0x7;
 if(fnc == 0)
  return;

 // First pass: normal and normal-with-Z sprites.  This is what Maze2D uses
 // through ASLMakeSimpleSprite()/ASLExecuteSprite().  More complex zoom/rotate
 // ASL modes still fall through to the conservative rectangle path so they at
 // least become visible instead of being silently dropped.
 const int src_bank = head & 0x1F;
 const bool hflip = (head & 0x0100) != 0;
 const bool vflip = (head & 0x0200) != 0;
 const uint16 cntl = sct[1];
 const bool zcmp = (cntl & 0x2000) != 0;
 const bool zwen = (cntl & 0x1000) != 0;
 const int z = (int)sct[6];

 int dx = SignExtendBits(sct[2] & 0x03FF, 10);
 int dy = SignExtendBits(sct[3] & 0x01FF, 9);
 const int dw = SpriteDim(sct[4] & 0x00FF);
 const int dh = SpriteDim((sct[4] >> 8) & 0x00FF);
 const int sx0 = sct[5] & 0x00FF;
 const int sy0 = (sct[5] >> 8) & 0x00FF;
 const int encoded_sw = sct[7] & 0x00FF;
 const int encoded_sh = (sct[7] >> 8) & 0x00FF;
 const bool normal_sprite = (fnc == 1 || fnc == 2);

 // GMAKER ASL simple sprites only initialise SCT words 0..5 (and word 6 for
 // normal-with-Z).  Word 7 is the source-size field used by zoom/rotate modes,
 // so treating stale word-7 contents as a normal sprite source size causes the
 // one-frame ASL turn glitch visible in Maze2D.
 const int sw = (!normal_sprite && sct[7]) ? SpriteDim(encoded_sw) : dw;
 const int sh = (!normal_sprite && sct[7]) ? SpriteDim(encoded_sh) : dh;

 // GMAKER's ASLMakeSimpleSprite() defines normal sprites entirely by the
 // display/source positions and the display size.  Do not apply the previous
 // field-height doubling hack here; it turns psxdemo's 16x16 ball into a
 // skewed smear and is not part of the documented simple-sprite SCT layout.
 // C12M is texture storage width here, not an automatic direct-RGB display
 // mode.  The supplied psxdemo and Maze2D ASL sprites both rely on the VCE
 // indexed/I-C palette path for final colours.
 const bool direct_c12m_bgr = false;
 const bool expand_8level_bgr = direct_c12m_bgr && SpriteSourceLooks8LevelBGR(src_bank, sx0, sy0, sw, sh);
 const int render_dh = dh;
 // Word 1 bit 14 is PDD in the HuC6273 sprite supplement: pixel draw disable.
 // Respect it so collision/flag-only SCTs do not leave visible ASL garbage.
 if(cntl & 0x4000)
  return;

 int clip_l = SPWindowX[0] & 0x1FF;
 int clip_t = SPWindowY[0] & 0x1FF;
 int clip_r = SPWindowX[1] & 0x1FF;
 int clip_b = SPWindowY[1] & 0x1FF;
 if(clip_r <= clip_l) { clip_l = 0; clip_r = FB_W - 1; }
 if(clip_b <= clip_t) { clip_t = 0; clip_b = FB_H - 1; }
 clip_l = huc_max_int(0, huc_min_int(FB_W - 1, clip_l));
 clip_r = huc_max_int(0, huc_min_int(FB_W - 1, clip_r));
 clip_t = huc_max_int(0, huc_min_int(FB_H - 1, clip_t));
 clip_b = huc_max_int(0, huc_min_int(FB_H - 1, clip_b));

 for(int oy = 0; oy < render_dh; oy++)
 {
  const int py = dy + oy;
  if(py < clip_t || py > clip_b)
   continue;
  int sy = sy0 + ((oy * sh) / render_dh);
  if(vflip) sy = sy0 + (sh - 1) - ((oy * sh) / render_dh);
  sy &= 0xFF;

  for(int ox = 0; ox < dw; ox++)
  {
   const int px = dx + ox;
   if(px < clip_l || px > clip_r)
    continue;
   int sx = sx0 + ((ox * sw) / dw);
   if(hflip) sx = sx0 + (sw - 1) - ((ox * sw) / dw);
   sx &= 0xFF;

   if(zcmp) CntPxSlow++; else CntPxFast++;
   const uint16 pix = ReadSpriteSourceWord(src_bank, sx, sy);
   // GMAKER AID indexed assets reserve index 0/1 for background/transparent
   // pixels.  Direct C12M sprites only key zero; value 1 is a valid low
   // intensity direct-colour texel.
   if(pix == 0 || (!direct_c12m_bgr && pix == 1))
    continue;

   const int o = py * FB_W + px;
   if(zcmp && !(PE[3] & PE_CTRL_ZCAT) && FrameValid[DrawBuffer][o] &&
      z <= ZBuffer[o])
    continue;
   FrameBuffer[DrawBuffer][o] = SpritePixelToNative(pix, direct_c12m_bgr, expand_8level_bgr);
   FrameValid[DrawBuffer][o] = HUC6273_PIX_DRAWN;
   if(zwen || fnc == 2)
    ZBuffer[o] = z;
   else if(!zcmp)
    ZBuffer[o] = 0x7FFFFFFF;
  }
 }
}

static void ExecuteSpritesRange(uint16 sct_address, uint16 sprite_control)
{
 const int sct_bank = (sct_address >> 3) & 0x1F;
 const int sct_start = ((sct_address & 0x0007) << 8) | ((sprite_control >> 8) & 0x00FF);
 int count = ((sct_address >> 8) & 0x0007) << 8;
 count |= sprite_control & 0x00FF;
 if(count == 0)
  count = 2048;
 if(count > 2048)
  count = 2048;
 for(int i = 0; i < count; i++)
  DrawSpriteEntry(sct_bank, (sct_start + i) & 0x7FF);
}

static void ReplayLastSprites(void)
{
 if(!LastSpriteValid || ReplayingSprites)
  return;
 ReplayingSprites = true;
 ExecuteSpritesRange(LastSpriteSCTAddress, LastSpriteControl);
 ReplayingSprites = false;
}

static void ExecuteSprites(void)
{
 LastSpriteSCTAddress = SCTAddress;
 LastSpriteControl = SpriteControl;
 LastSpriteValid = true;
 ExecuteSpritesRange(SCTAddress, SpriteControl);
}

static void GetTEWindowClip(int* clip_l, int* clip_t, int* clip_r, int* clip_b)
{
 int l = TE[65] & 0x1FF;
 int t = TE[67] & 0x1FF;
 int r = TE[69] & 0x1FF;
 int b = TE[71] & 0x1FF;

 if(r <= l) { l = 0; r = FB_W - 1; }
 if(b <= t) { t = 0; b = FB_H - 1; }

 *clip_l = huc_max_int(0, huc_min_int(FB_W - 1, l));
 *clip_r = huc_max_int(0, huc_min_int(FB_W - 1, r));
 *clip_t = huc_max_int(0, huc_min_int(FB_H - 1, t));
 *clip_b = huc_max_int(0, huc_min_int(FB_H - 1, b));
}

// ---------------------------------------------------------------------------
// 3D primitive rasterisation, per NEC "HuC6273 device manual" (GMAKER
// DOC/DEVICE/C6273_2 sec. 1.2.3-1.2.4, C6273_5 sec. 3.5 and 3.6.8.6):
//  - [x' y' z' W] = Mobj . [x y z 1];  xs = x'/W, ys = y'/W, zs = z'/W.
//  - Reject: a primitive with any vertex outside the normalised-screen logical
//    region (-7.0 <= x < 8.0, -7.0 <= y < 8.0, -8.5 <= z <= 7.0) or outside the
//    device-coordinate logical region (-4088 <= x,y < 4096) is discarded whole.
//    A vertex behind the viewpoint (view Z > f, i.e. W <= 0) also rejects.
//  - Z clip: the displayed range is -1.0 < zs <= 0.0; primitives inside the
//    logical region are clipped against the zs = 0 and zs = -1 planes.
//  - Backface cull (TE control BCE): 3-vertex cross product; list triangles
//    and odd strip triangles are front-facing when counter-clockwise (the strip
//    decoder swaps even triangles so every triangle arrives here front=CCW).
// ---------------------------------------------------------------------------

static bool VtxInLogicalRegion(const Vtx* v)
{
 if(!(v->cw > 0.0f))
  return false;
 const float xs = v->cx / v->cw;
 const float ys = v->cy / v->cw;
 const float zs = v->cz / v->cw;
 if(!(xs >= -7.0f && xs < 8.0f && ys >= -7.0f && ys < 8.0f && zs >= -8.5f && zs <= 7.0f))
  return false;
 if(!(v->fsx >= -4088.0f && v->fsx < 4096.0f && v->fsy >= -4088.0f && v->fsy < 4096.0f))
  return false;
 return true;
}

static Vtx LerpClipVtx(const Vtx* A, const Vtx* B, float t)
{
 Vtx o = *A;
 o.cx = A->cx + (B->cx - A->cx) * t;
 o.cy = A->cy + (B->cy - A->cy) * t;
 o.cz = A->cz + (B->cz - A->cz) * t;
 o.cw = A->cw + (B->cw - A->cw) * t;
 o.raw_x = A->raw_x + (B->raw_x - A->raw_x) * t;
 o.raw_y = A->raw_y + (B->raw_y - A->raw_y) * t;
 o.raw_z = A->raw_z + (B->raw_z - A->raw_z) * t;
 o.u = A->u + (B->u - A->u) * t;
 o.v = A->v + (B->v - A->v) * t;
 o.shade = A->shade + (B->shade - A->shade) * t;
 // I is interpolated (NEC 1.2.9); C stays that of A.  A colour word of 0
 // means "default colour" and is left as it is.
 if(A->color && B->color)
 {
  const float ia = (float)((A->color >> 6) & 0x3F), ib = (float)((B->color >> 6) & 0x3F);
  const int i = (int)lrintf(ia + (ib - ia) * t);
  o.color = (uint16)((A->color & ~0x0FC0) | ((i & 0x3F) << 6));
 }
 VtxFinishProjection(&o);
 return o;
}

// Clip a convex polygon against one Z plane.  plane 0: z' <= 0 (zs <= 0);
// plane 1: z' + W > 0 (zs > -1).  Distances are linear in clip space.
static int ClipPolyZ(const Vtx* in, int n, Vtx* out, int plane)
{
 int m = 0;
 for(int i = 0; i < n; i++)
 {
  const Vtx* A = &in[i];
  const Vtx* B = &in[(i + 1) % n];
  const float da = plane ? (A->cz + A->cw) : -A->cz;
  const float db = plane ? (B->cz + B->cw) : -B->cz;
  const bool ina = plane ? (da > 0.0f) : (da >= 0.0f);
  const bool inb = plane ? (db > 0.0f) : (db >= 0.0f);
  if(ina)
   out[m++] = *A;
  if(ina != inb)
   out[m++] = LerpClipVtx(A, B, da / (da - db));
 }
 return m;
}

// Texture buffer texel fetch (NEC 1.4.4 / 1.9): the buffer is 9 bits deep, one
// texel per address; u,v (0.8.0) plus the TE uv offset register address a
// 256x256 texel bank selected by PE texture select.  Bits above 8 written
// through the unpack window are discarded by the hardware.
static uint16 FetchTexel9(float uf, float vf)
{
 const int tx = ((int)floorf(uf) + (int)TE[86]) & (TEX_W - 1);
 const int ty = ((int)floorf(vf) + (int)TE[88]) & (TEX_H - 1);
 const uint32 o = (uint32)(ty * TEX_W + tx);
 const int bank = PE[0] & (TEX_BANKS - 1);
 if(TextureUnpackedValid[bank][o])
  return TextureUnpacked[bank][o] & 0x01FF;
 if(TextureValid[bank][o])
  return Texture[bank][o] & 0x01FF;
 return 0;
}

static void RasterTriangle(const Vtx* pa, const Vtx* pb, const Vtx* pc, bool textured)
{
 const Vtx a = *pa;
 const Vtx b = *pb;
 const Vtx c = *pc;
 const int sp_shift = 4;
 const int sp_one = 1 << sp_shift;
 const int ax = (int)lrintf(a.fsx * (float)sp_one), ay = (int)lrintf(a.fsy * (float)sp_one);
 const int bx = (int)lrintf(b.fsx * (float)sp_one), by = (int)lrintf(b.fsy * (float)sp_one);
 const int cx = (int)lrintf(c.fsx * (float)sp_one), cy = (int)lrintf(c.fsy * (float)sp_one);
 const int64 area64 = Edge64(ax, ay, bx, by, cx, cy);
 if(area64 == 0)
  return;

 int minx = huc_max_int(0, (huc_min_int(ax, huc_min_int(bx, cx)) - (sp_one >> 1)) >> sp_shift);
 int maxx = huc_min_int(FB_W - 1, (huc_max_int(ax, huc_max_int(bx, cx)) + (sp_one >> 1) + sp_one - 1) >> sp_shift);
 int miny = huc_max_int(0, (huc_min_int(ay, huc_min_int(by, cy)) - (sp_one >> 1)) >> sp_shift);
 int maxy = huc_min_int(FB_H - 1, (huc_max_int(ay, huc_max_int(by, cy)) + (sp_one >> 1) + sp_one - 1) >> sp_shift);
 int clip_l, clip_t, clip_r, clip_b;
 GetTEWindowClip(&clip_l, &clip_t, &clip_r, &clip_b);
 minx = huc_max_int(minx, clip_l);
 maxx = huc_min_int(maxx, clip_r);
 miny = huc_max_int(miny, clip_t);
 maxy = huc_min_int(maxy, clip_b);
 if(minx > maxx || miny > maxy)
  return;

 const uint16 default_word = TE[90] ? TE[90] : 0x0FFF;
 const uint16 ca = a.color ? a.color : default_word;
 const uint16 cb = b.color ? b.color : default_word;
 const uint16 cc = c.color ? c.color : default_word;
 const float inv_area = 1.0f / (float)area64;
 // Drawing time: Z-compared or texture-mapped pixels are the slow kind.
 uint64* const px_count = (textured || !(PE[3] & PE_CTRL_ZCAT)) ? &CntPxSlow : &CntPxFast;
 // Perspective-correct attribute weights (1/W).  NEC only states that texture
 // coordinates are interpolated by the hardware; perspective correction is an
 // assumption carried over from the existing standard path.
 const float iwa = 1.0f / a.cw, iwb = 1.0f / b.cw, iwc = 1.0f / c.cw;
 const bool ic_mode = (TE[255] & TE_CTRL_ICM) && !(PE[3] & PE_CTRL_C12M);
 // Lit I (6-bit field scale) per vertex: colour-word I x lighting factor.
 const float lit_ia = (float)((ca >> 6) & 0x3F) * a.shade;
 const float lit_ib = (float)((cb >> 6) & 0x3F) * b.shade;
 const float lit_ic = (float)((cc >> 6) & 0x3F) * c.shade;

 for(int y = miny; y <= maxy; y++)
  for(int x = minx; x <= maxx; x++)
  {
   const int px = (x << sp_shift) + (sp_one >> 1);
   const int py = (y << sp_shift) + (sp_one >> 1);
   const int64 w0 = Edge64(bx, by, cx, cy, px, py);
   const int64 w1 = Edge64(cx, cy, ax, ay, px, py);
   const int64 w2 = Edge64(ax, ay, bx, by, px, py);
   if((area64 > 0 && (w0 < 0 || w1 < 0 || w2 < 0)) || (area64 < 0 && (w0 > 0 || w1 > 0 || w2 > 0)))
    continue;
   (*px_count)++;

   const float fa = w0 * inv_area;
   const float fb = w1 * inv_area;
   const float fc = w2 * inv_area;

   const int o = y * FB_W + x;
   // zs is affine in screen space, so it interpolates linearly.
   const int32 zi = (int32)lrintf(a.zi * fa + b.zi * fb + c.zi * fc);
   // NEC 1.6.5: the source pixel is written when Zd <= Zs.
   if(!(PE[3] & PE_CTRL_ZCAT) && FrameValid[DrawBuffer][o] && zi < ZBuffer[o])
    continue;

   const float shade = a.shade * fa + b.shade * fb + c.shade * fc;
   MDFN_Pixel color;
   if(ic_mode)
   {
    // NEC 1.2.9: I is interpolated from the vertices (after lighting);
    // C is taken from the last vertex of the triangle.  I and the lighting
    // factor are interpolated as the first vertex's value plus weighted
    // differences, so a value that is the same at all three vertices comes
    // out exactly; the truncation of I would otherwise drop a whole level on
    // a rounding error.
    const float lit_i = lit_ia + (lit_ib - lit_ia) * fb + (lit_ic - lit_ia) * fc;
    const float shade_ic = a.shade + (b.shade - a.shade) * fb + (c.shade - a.shade) * fc;
    if(textured)
    {
     const float pa_ = fa * iwa, pb_ = fb * iwb, pc_ = fc * iwc;
     const float denom = pa_ + pb_ + pc_;
     float uf, vf;
     if(fabsf(denom) > 1.0e-12f)
     {
      uf = (a.u * pa_ + b.u * pb_ + c.u * pc_) / denom;
      vf = (a.v * pa_ + b.v * pb_ + c.v * pc_) / denom;
     }
     else
     {
      uf = a.u * fa + b.u * fb + c.u * fc;
      vf = a.v * fa + b.v * fb + c.v * fc;
     }
     const uint16 t9 = FetchTexel9(uf, vf);
     // NEC 1.6.5: a texel with C == 0 and I == 0 leaves colour and Z unchanged.
     if(t9 == 0)
      continue;
     uint16 w12 = ExpandIC(t9, ICMask());
     float factor = 1.0f;
     if(PE[3] & PE_CTRL_TLEN)
     {
      if(PE[3] & PE_CTRL_TLM)
      {
       // TLM=1: the I component comes from the rasteriser (lit primitive I).
       int i6 = (int)lit_i;
       if(i6 < 0) i6 = 0;
       if(i6 > 0x3F) i6 = 0x3F;
       w12 = (uint16)((w12 & 0x3F) | (i6 << 6));
      }
      else
       factor = shade_ic;
     }
     color = ICWordLitToNative(w12, factor);
    }
    else
    {
     // Interpolated lit I applied to the last vertex's C field.
     color = ICLitColorToNative(cc, lit_i);
    }
   }
   else
   {
    const uint16 base_word = InterpWord(ca, cb, cc, fa, fb, fc);
    if(textured)
    {
     const float pa_ = fa * iwa, pb_ = fb * iwb, pc_ = fc * iwc;
     const float denom = pa_ + pb_ + pc_;
     const float uf = fabsf(denom) > 1.0e-12f ? (a.u * pa_ + b.u * pb_ + c.u * pc_) / denom : a.u * fa + b.u * fb + c.u * fc;
     const float vf = fabsf(denom) > 1.0e-12f ? (a.v * pa_ + b.v * pb_ + c.v * pc_) / denom : a.v * fa + b.v * fb + c.v * fc;
     if(!SampleTexture((uint16)(int)floorf(uf), (uint16)(int)floorf(vf), base_word, shade, &color, true))
      continue;
    }
    else
     color = ColorWordToNativeShaded(base_word, shade);
   }

   FrameBuffer[DrawBuffer][o] = color;
   FrameValid[DrawBuffer][o] = HUC6273_PIX_DRAWN;
   ZBuffer[o] = zi;
  }
}

// Reject mode (TE control RJM, NEC C6273_5 sec. 3.6.8.6 and its note 3).
// RJM=0 discards a triangle with two or three vertices at the same screen
// x,y.  RJM=1 draws it: two coinciding vertices give a straight line to the
// third vertex, three give a single point.  Z and I are not interpolated
// along it; the whole line takes the Z and I of the triangle's first vertex,
// meaning the first in the order the triangle was input in the command
// packet (f below).  Colour otherwise follows NEC 1.2.9 as for every
// primitive: in I-C mode C comes from the last vertex, and in index mode the
// last vertex's colour word is used for the whole primitive.
//
// NEC does not state the following; these are the choices made here:
//  - Coincidence is tested on the 1/16-pixel device coordinates this
//    rasteriser already uses.  The hardware's own precision is undocumented.
//  - Zero-area triangles with three distinct vertices (collinear) are still
//    discarded.  NEC describes only coinciding vertices.
//  - NEC names only Z and I as not interpolated, so texture u,v are
//    interpolated along the line (perspective-correct, as for triangles),
//    from the earlier (packet order) vertex of the coinciding pair, p, to the
//    third vertex, q.
//  - One pixel per step along the major axis between the pixels holding the
//    end points, inside the TE window clip, with the same Z compare and Z
//    write as filled triangles.  The backface cull is not applied (a line has
//    no facing), and a line that would need Z clipping is not drawn.
static void DrawCoincidentLine(const Vtx* f, const Vtx* last, const Vtx* p, const Vtx* q, bool textured,
                               int x0_16, int y0_16, int x1_16, int y1_16)
{
 const uint16 default_word = TE[90] ? TE[90] : 0x0FFF;
 const uint16 cf = f->color ? f->color : default_word;
 const uint16 cl = last->color ? last->color : default_word;
 const bool ic_mode = (TE[255] & TE_CTRL_ICM) && !(PE[3] & PE_CTRL_C12M);
 const float lit_i = (float)((cf >> 6) & 0x3F) * f->shade;
 const uint16 base_word = (uint16)(cl & 0x1FFF);
 MDFN_Pixel flat = 0;

 // Untextured: one colour for the whole line.  I and the lighting factor come
 // from the first vertex; C (I-C mode) or the colour word (index mode) from
 // the last.
 if(!textured)
 {
  if(ic_mode)
   flat = ICLitColorToNative(cl, lit_i);
  else
   flat = ColorWordToNativeShaded(base_word, f->shade);
 }

 const int32 zi = f->zi;
 const float iwp = 1.0f / p->cw, iwq = 1.0f / q->cw;
 int clip_l, clip_t, clip_r, clip_b;
 GetTEWindowClip(&clip_l, &clip_t, &clip_r, &clip_b);

 int x = x0_16 >> 4, y = y0_16 >> 4;
 const int x1 = x1_16 >> 4, y1 = y1_16 >> 4;
 const int dx = abs(x1 - x), sx = x < x1 ? 1 : -1;
 const int dy = -abs(y1 - y), sy = y < y1 ? 1 : -1;
 const int steps = huc_max_int(dx, -dy);
 int err = dx + dy;
 for(int k = 0; ; k++)
 {
  if(x >= clip_l && x <= clip_r && y >= clip_t && y <= clip_b)
  {
   const int o = y * FB_W + x;
   if(textured || !(PE[3] & PE_CTRL_ZCAT)) CntPxSlow++; else CntPxFast++;
   if((PE[3] & PE_CTRL_ZCAT) || !FrameValid[DrawBuffer][o] || zi >= ZBuffer[o])
   {
    MDFN_Pixel color = flat;
    bool draw = true;
    if(textured)
    {
     const float t = steps ? (float)k / (float)steps : 0.0f;
     const float wp = (1.0f - t) * iwp, wq = t * iwq;
     const float uf = (p->u * wp + q->u * wq) / (wp + wq);
     const float vf = (p->v * wp + q->v * wq) / (wp + wq);
     if(ic_mode)
     {
      const uint16 t9 = FetchTexel9(uf, vf);
      if(t9 == 0)
       draw = false;
      else
      {
       uint16 w12 = ExpandIC(t9, ICMask());
       float factor = 1.0f;
       if(PE[3] & PE_CTRL_TLEN)
       {
        if(PE[3] & PE_CTRL_TLM)
        {
         int i6 = (int)lit_i;
         if(i6 < 0) i6 = 0;
         if(i6 > 0x3F) i6 = 0x3F;
         w12 = (uint16)((w12 & 0x3F) | (i6 << 6));
        }
        else
         factor = f->shade;
       }
       color = ICWordLitToNative(w12, factor);
      }
     }
     else
      draw = SampleTexture((uint16)(int)floorf(uf), (uint16)(int)floorf(vf), base_word, f->shade, &color, true);
    }
    if(draw)
    {
     FrameBuffer[DrawBuffer][o] = color;
     FrameValid[DrawBuffer][o] = HUC6273_PIX_DRAWN;
     ZBuffer[o] = zi;
    }
   }
  }
  if(x == x1 && y == y1)
   break;
  const int e2 = 2 * err;
  if(e2 >= dy) { err += dy; x += sx; }
  if(e2 <= dx) { err += dx; y += sy; }
 }
}

static void DrawTriangleEx(Vtx a, Vtx b, Vtx c, bool textured, CullMode cull_mode, int first_idx)
{
 (void)cull_mode;

 // Reject (NEC 1.2.4.1).
 if(!VtxInLogicalRegion(&a) || !VtxInLogicalRegion(&b) || !VtxInLogicalRegion(&c))
  return;

 // RJM=1: coinciding vertices are drawn as a line or point (see above).
 // first_idx is 0 when a is the packet's first vertex, 1 when b is (strip
 // triangles that were swapped so that a front-facing one arrives
 // counter-clockwise).
 if(TE[255] & TE_CTRL_RJM)
 {
  const int ax = (int)lrintf(a.fsx * 16.0f), ay = (int)lrintf(a.fsy * 16.0f);
  const int bx = (int)lrintf(b.fsx * 16.0f), by = (int)lrintf(b.fsy * 16.0f);
  const int cx = (int)lrintf(c.fsx * 16.0f), cy = (int)lrintf(c.fsy * 16.0f);
  const bool ab = (ax == bx && ay == by), bc = (bx == cx && by == cy), ac = (ax == cx && ay == cy);
  if(ab || bc || ac)
  {
   if(!(a.zs <= 0.0f && b.zs <= 0.0f && c.zs <= 0.0f && a.zs > -1.0f && b.zs > -1.0f && c.zs > -1.0f))
    return;
   const Vtx* f = first_idx ? &b : &a;
   if(ab)
    DrawCoincidentLine(f, &c, f, &c, textured, ax, ay, cx, cy);
   else if(bc)
    DrawCoincidentLine(f, &c, &b, &a, textured, bx, by, ax, ay);
   else
    DrawCoincidentLine(f, &c, &a, &b, textured, ax, ay, bx, by);
   return;
  }
 }

 // Backface cull (NEC 3.6.8.6 note 2): sign of the projected 3-vertex area.
 {
  const int64 area = (int64)lrintf((c.fsx - a.fsx) * 16.0f) * (int64)lrintf((b.fsy - a.fsy) * 16.0f) -
                     (int64)lrintf((c.fsy - a.fsy) * 16.0f) * (int64)lrintf((b.fsx - a.fsx) * 16.0f);
  if(area == 0)
   return;
  if((TE[255] & TE_CTRL_BCE) && area < 0)
   return;
 }

 // Z clip against zs = 0 and zs = -1 (NEC 1.2.4.3).
 const bool need_clip = !(a.zs <= 0.0f && b.zs <= 0.0f && c.zs <= 0.0f &&
                          a.zs > -1.0f && b.zs > -1.0f && c.zs > -1.0f);
 if(!need_clip)
 {
  RasterTriangle(&a, &b, &c, textured);
  return;
 }

 Vtx p0[3] = { a, b, c };
 Vtx p1[8], p2[8];
 int n = ClipPolyZ(p0, 3, p1, 0);
 if(n < 3)
  return;
 n = ClipPolyZ(p1, n, p2, 1);
 for(int i = 1; i + 1 < n; i++)
  RasterTriangle(&p2[0], &p2[i], &p2[i + 1], textured);
}

// Vertices passed in packet order (a is the first vertex).
static void DrawTriangle(Vtx a, Vtx b, Vtx c, bool textured, CullMode cull_mode)
{
 DrawTriangleEx(a, b, c, textured, cull_mode, 0);
}

static void DrawLine(int x0, int y0, int x1, int y1, uint16 color)
{
 MDFN_Pixel native = ColorWordToNative(color ? color : (TE[90] ? TE[90] : 0x0FFF));
 int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
 int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
 int err = dx + dy;
 while(true)
 {
  if(x0 >= 0 && x0 < FB_W && y0 >= 0 && y0 < FB_H)
  {
   int o = y0 * FB_W + x0;
   if(!(PE[3] & PE_CTRL_ZCAT)) CntPxSlow++; else CntPxFast++;
   FrameBuffer[DrawBuffer][o] = native;
   FrameValid[DrawBuffer][o] = HUC6273_PIX_DRAWN;
  }
  if(x0 == x1 && y0 == y1)
   break;
  int e2 = 2 * err;
  if(e2 >= dy) { err += dy; x0 += sx; }
  if(e2 <= dx) { err += dx; y0 += sy; }
 }
}

static void Complete(uint16 bits)
{
 if(InCommand)
 {
  // ProcessCommandTimed posts these when the command is done.
  CmdBits |= bits;
  MiscStatus &= ~((1 << 3) | (1 << 4) | (1 << 11) | (1 << 12));
  ErrorStatus = 0x0011;
  return;
 }
 if(PEStalled)
 {
  HeldPEBits |= bits & INT_PESYNC;
  bits &= ~INT_PESYNC;
 }
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
   for(size_t i=0;i<n && i<16;i++)
    TE[a[i]] = body[i];
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
   // NEC manual 3.6.8.14: the TE control register (address 255) cannot be
   // written by Write TE register (with address); only option 6 writes it.
   // Same Game FX relies on this: it repeatedly issues 8F03 00FF 0035
   // (SPEN on).  Captures of the game show no specular washout; honouring
   // the write turns its pieces white.
   for(size_t i = 0; i + 1 < n; i += 2)
   {
    const uint8 addr = body[i] & 0xFF;
    if(addr != 255)
     TE[addr] = body[i + 1];
   }
   break;
 }
 Complete(INT_TESYNC);
}

static void DoWritePE(uint8 option, const uint16* body, size_t n)
{
 if(n)
  PE[option & 0xF] = body[0];


 if((option & 0xF) == 5 && n)
 {
  const uint16 v = body[0];
  // Software normally requests swap/VSync through frame control.  Use the
  // documented SWP bits loosely; hidden-clear is handled by explicit fill/clear
  // commands to avoid erasing still-pending geometry.
  if(v & 0x0001)
   RequestBufferSwap(v);
  else if(SwapPending)
   PELockedUntilFrame = true;
  Complete(INT_PESYNC);
 }
 else
  Complete(INT_PESYNC);
}

static void DoPutImage(uint8 option, const uint16* body, size_t n)
{
 if(option == 1 && n >= 4)
 {
  int xl = S16(body[0]), yt = S16(body[1]), xr = S16(body[2]), yb = S16(body[3]);
  if(xl > xr) huc_swap_int(&xl, &xr);
  if(yt > yb) huc_swap_int(&yt, &yb);
  size_t p = 4;
  for(int y = yt; y <= yb && p < n; y++)
   for(int x = xl; x <= xr && p < n; x++, p++)
    if(x >= 0 && x < TEX_W && y >= 0 && y < TEX_H)
    {
     int o = y * TEX_W + x;
     const int bank = PE[0] & (TEX_BANKS - 1);
     Texture[bank][o] = body[p];
     TextureValid[bank][o] = 1;
     // NEC 1.4.4: Put image to texture buffer stores a 9-bit texel; in I-C
     // mode (C12M=0) the hardware packs the 12-bit colour through the PE I-C
     // mask.  Mirror that into the 9-bit texel view the rasteriser samples.
     TextureUnpacked[bank][o] = (PE[3] & PE_CTRL_C12M) ? (uint16)(body[p] & 0x01FF) : CompressIC(body[p], ICMask());
     TextureUnpackedValid[bank][o] = 1;
    }
  CntPxFast += p - 4;
 }
 else if(option == 0 && n >= 5)
 {
  size_t p = 5; // z, xl, yt, xr, yb
  int xl = S16(body[1]), yt = S16(body[2]), xr = S16(body[3]), yb = S16(body[4]);
  if(xl > xr) huc_swap_int(&xl, &xr);
  if(yt > yb) huc_swap_int(&yt, &yb);
  for(int y = yt; y <= yb && p < n; y++)
   for(int x = xl; x <= xr && p < n; x++, p++)
    if(x >= 0 && x < FB_W && y >= 0 && y < FB_H)
    {
     int o = y * FB_W + x;
     FrameBuffer[DrawBuffer][o] = ColorWordToNative(body[p]);
     FrameValid[DrawBuffer][o] = HUC6273_PIX_DRAWN;
    }
  CntPxFast += p - 5;
 }
 else if(option == 2 && n >= 4)
 {
  size_t p = 4;
  int xl = S16(body[0]), yt = S16(body[1]), xr = S16(body[2]), yb = S16(body[3]);
  if(xl > xr) huc_swap_int(&xl, &xr);
  if(yt > yb) huc_swap_int(&yt, &yb);
  for(int y = yt; y <= yb && p + 1 < n; y++)
   for(int x = xl; x <= xr && p + 1 < n; x++, p += 2)
    if(x >= 0 && x < FB_W && y >= 0 && y < FB_H)
    {
     int o = y * FB_W + x;
     FrameBuffer[DrawBuffer][o] = ColorWordToNative(body[p]);
     FrameValid[DrawBuffer][o] = HUC6273_PIX_DRAWN;
    }
  CntPxFast += (p - 4) / 2;
 }
 Complete(INT_PESYNC);
}

static void DoTriangleList(uint8 option, const uint16* body, size_t n)
{
 size_t p = 0;
 const uint16 dc = TE[90] ? TE[90] : 0x0FFF;

 if(option == 0) // Vertex color, no normal.
 {
  while(p + 12 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++) { uint16 c = body[p++]; ReadXYZ(body,n,&p,&v[i],c,0,0); }
   DrawTriangle(v[0],v[1],v[2],false,CULL_PROJECTED_AREA);
  }
 }
 else if(option == 1) // Vertex color + vertex normal.
 {
  while(p + 21 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++)
   {
    uint16 c = body[p++];
    ReadXYZ(body,n,&p,&v[i],c,0,0);
    ReadVertexNormalShade(body,n,&p,&v[i],false);
   }
   DrawTriangle(v[0],v[1],v[2],false,CULL_TRANSFORMED_NORMAL);
  }
 }
 else if(option == 2) // Facet color + facet normal.
 {
  while(p + 13 <= n)
  {
   Vtx v[3];
   ReadXYZ(body,n,&p,&v[0],0,0,0);
   ReadXYZ(body,n,&p,&v[1],0,0,0);
   uint16 c = body[p++];
   ReadXYZ(body,n,&p,&v[2],c,0,0);
   float sh = 1.0f;
   float nx = 0.0f, ny = 0.0f, nz = 0.0f;
   float snx = 0.0f, sny = 0.0f, snz = 0.0f;
   ReadNormalShadeEx(body,n,&p,&sh,&nx,&ny,&nz,&snx,&sny,&snz,false);
   for(int i = 0; i < 3; i++) { v[i].color = c; ApplyFacetNormalEx(&v[i], sh, nx, ny, nz, snx, sny, snz); }
   DrawTriangle(v[0],v[1],v[2],false,CULL_PROJECTED_AREA);
  }
 }
 else if(option == 4) // Texture, no normal.
 {
  while(p + 15 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++)
   {
    uint16 u = body[p++], vv = body[p++];
    ReadXYZ(body,n,&p,&v[i],dc,u,vv);
   }
   DrawTriangle(v[0],v[1],v[2],true,CULL_PROJECTED_AREA);
  }
 }
 else if(option == 5) // Texture + vertex normal.
 {
  while(p + 24 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++)
   {
    uint16 u = body[p++], vv = body[p++];
    ReadXYZ(body,n,&p,&v[i],dc,u,vv);
    ReadVertexNormalShade(body,n,&p,&v[i],true);
   }
   DrawTriangle(v[0],v[1],v[2],true,CULL_TRANSFORMED_NORMAL);
  }
 }
 else if(option == 6) // Texture + facet normal.
 {
  while(p + 18 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++)
   {
    uint16 u = body[p++], vv = body[p++];
    ReadXYZ(body,n,&p,&v[i],dc,u,vv);
   }
   float sh = 1.0f;
   float nx = 0.0f, ny = 0.0f, nz = 0.0f;
   float snx = 0.0f, sny = 0.0f, snz = 0.0f;
   ReadNormalShadeEx(body,n,&p,&sh,&nx,&ny,&nz,&snx,&sny,&snz,true);
   for(int i = 0; i < 3; i++) ApplyFacetNormalEx(&v[i], sh, nx, ny, nz, snx, sny, snz);
   DrawTriangle(v[0],v[1],v[2],true,CULL_PROJECTED_AREA);
  }
 }
 else if(option == 8) // Default color, no normal.
 {
  while(p + 9 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++) ReadXYZ(body,n,&p,&v[i],dc,0,0);
   DrawTriangle(v[0],v[1],v[2],false,CULL_PROJECTED_AREA);
  }
 }
 else if(option == 9) // Default color + vertex normal.
 {
  while(p + 18 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++)
   {
    ReadXYZ(body,n,&p,&v[i],dc,0,0);
    ReadVertexNormalShade(body,n,&p,&v[i],false);
   }
   DrawTriangle(v[0],v[1],v[2],false,CULL_TRANSFORMED_NORMAL);
  }
 }
 else if(option == 0xA) // Default color + facet normal.
 {
  while(p + 12 <= n)
  {
   Vtx v[3];
   for(int i=0;i<3;i++) ReadXYZ(body,n,&p,&v[i],dc,0,0);
   float sh = 1.0f;
   float nx = 0.0f, ny = 0.0f, nz = 0.0f;
   float snx = 0.0f, sny = 0.0f, snz = 0.0f;
   ReadNormalShadeEx(body,n,&p,&sh,&nx,&ny,&nz,&snx,&sny,&snz,false);
   for(int i = 0; i < 3; i++) ApplyFacetNormalEx(&v[i], sh, nx, ny, nz, snx, sny, snz);
   DrawTriangle(v[0],v[1],v[2],false,CULL_PROJECTED_AREA);
  }
 }
 Complete(INT_PESYNC);
}

static void DoTriangleStrip(uint8 option, const uint16* body, size_t n)
{
 Vtx verts[0x400];
 size_t verts_count = 0;
 size_t p = 0;
 const uint16 dc = TE[90] ? TE[90] : 0x0FFF;

 if(option == 2 || option == 6 || option == 0xA) // Facet-normal strip.
 {
  const bool textured = (option == 6);
  while(p < n)
  {
   Vtx v;
   if(textured)
   {
    if(p + 5 > n) break;
    uint16 u = body[p++], vv = body[p++];
    if(!ReadXYZ(body, n, &p, &v, dc, u, vv)) break;
   }
   else if(option == 2)
   {
    // NEC 3.6.2.3: from the third vertex on, each vertex is preceded by the
    // facet colour of the triangle it completes (the first two carry none).
    uint16 fc = dc;
    if(verts_count >= 2)
    {
     if(p + 4 > n) break;
     fc = body[p++];
    }
    else if(p + 3 > n) break;
    if(!ReadXYZ(body, n, &p, &v, fc, 0, 0)) break;
   }
   else
   {
    if(p + 3 > n) break;
    if(!ReadXYZ(body, n, &p, &v, dc,0,0)) break;
   }
   if(verts_count < sizeof(verts) / sizeof(verts[0])) verts[verts_count++] = v;
   if(verts_count >= 3)
   {
    float sh = 1.0f;
    float nx = 0.0f, ny = 0.0f, nz = 0.0f;
    float snx = 0.0f, sny = 0.0f, snz = 0.0f;
    if(!ReadNormalShadeEx(body, n, &p, &sh, &nx, &ny, &nz, &snx, &sny, &snz, textured))
     break;
    Vtx a = verts[verts_count - 3];
    Vtx b = verts[verts_count - 2];
    Vtx c = verts[verts_count - 1];
    if(option == 2)
     a.color = b.color = c.color;
    ApplyFacetNormalEx(&a, sh, nx, ny, nz, snx, sny, snz);
    ApplyFacetNormalEx(&b, sh, nx, ny, nz, snx, sny, snz);
    ApplyFacetNormalEx(&c, sh, nx, ny, nz, snx, sny, snz);
    // Even-numbered triangles (counting from 1) are swapped so that a
    // front-facing one arrives counter-clockwise; the packet's first vertex
    // (a) is then argument 2.
    if((verts_count - 1) & 1)
     DrawTriangleEx(b, a, c, textured, CULL_PROJECTED_AREA, 1);
    else
     DrawTriangle(a, b, c, textured, CULL_PROJECTED_AREA);
   }
  }
  Complete(INT_PESYNC);
  return;
 }

 while(p < n)
 {
  Vtx v;
  if(option == 0) // Vertex color.
  {
   if(p + 4 > n) break;
   uint16 c = body[p++];
   ReadXYZ(body,n,&p,&v,c,0,0);
  }
  else if(option == 1) // Vertex color + vertex normal.
  {
   if(p + 7 > n) break;
   uint16 c = body[p++];
   ReadXYZ(body,n,&p,&v,c,0,0);
   ReadVertexNormalShade(body,n,&p,&v,false);
  }
  else if(option == 4) // Texture.
  {
   if(p + 5 > n) break;
   uint16 u = body[p++], vv = body[p++];
   ReadXYZ(body,n,&p,&v,dc,u,vv);
  }
  else if(option == 5) // Texture + vertex normal.
  {
   if(p + 8 > n) break;
   uint16 u = body[p++], vv = body[p++];
   ReadXYZ(body,n,&p,&v,dc,u,vv);
   ReadVertexNormalShade(body,n,&p,&v,true);
  }
  else if(option == 8) // Default color.
  {
   if(p + 3 > n) break;
   ReadXYZ(body,n,&p,&v,dc,0,0);
  }
  else if(option == 9) // Default color + vertex normal.
  {
   if(p + 6 > n) break;
   ReadXYZ(body,n,&p,&v,dc,0,0);
   ReadVertexNormalShade(body,n,&p,&v,false);
  }
  else
   break;
  if(verts_count < sizeof(verts) / sizeof(verts[0])) verts[verts_count++] = v;
 }
 for(size_t i = 2; i < verts_count; i++)
 {
  const bool textured = (option == 4 || option == 5);
  const CullMode cull_mode = (option == 1 || option == 5 || option == 9) ? CULL_TRANSFORMED_NORMAL : CULL_PROJECTED_AREA;
  // Swapped triangles keep the packet's first vertex (i-2) as argument 2.
  if(i & 1)
   DrawTriangleEx(verts[i-1], verts[i-2], verts[i], textured, cull_mode, 1);
  else
   DrawTriangle(verts[i-2], verts[i-1], verts[i], textured, cull_mode);
 }
 Complete(INT_PESYNC);
}

static void DoLines(uint8 option, const uint16* body, size_t n, bool list)
{
 Vtx verts[0x400];
 size_t verts_count = 0;
 size_t p = 0;
 while(p + 3 <= n)
 {
  Vtx v;
  uint16 c = TE[90] ? TE[90] : 0x0FFF;
  if(option == 0 && p + 4 <= n)
   c = body[p++];
  if(!ReadXYZ(body,n,&p,&v,c,0,0)) break;
  if(verts_count < sizeof(verts) / sizeof(verts[0])) verts[verts_count++] = v;
 }
 if(list)
 {
  for(size_t i = 1; i < verts_count; i += 2)
   DrawLine(verts[i-1].sx, verts[i-1].sy, verts[i].sx, verts[i].sy, verts[i-1].color);
 }
 else
 {
  for(size_t i = 1; i < verts_count; i++)
   DrawLine(verts[i-1].sx, verts[i-1].sy, verts[i].sx, verts[i].sy, verts[i-1].color);
 }
 Complete(INT_PESYNC);
}

static void DoMisc(uint8 option, const uint16* body, size_t n)
{
 if(option == 0 && n >= 6)
 {
  // Fill D/Z buffer: xleft, ytop, xright, ybottom, d, z.
  FillRect(S16(body[0]), S16(body[1]), S16(body[2]), S16(body[3]), ColorWordToNative(body[4]), Fixed115ToInt(body[5]));
  Complete(INT_PESYNC);
  return;
 }

 if(option >= 3 && option <= 0xF)
 {
  switch(option)
  {
   case 3: // matrix op 3 x vector (1.0.15)
    if(n >= 3)
    {
     const int16 vec[3] = { S16(body[0]), S16(body[1]), S16(body[2]) };
     MatrixVecOpNEC(vec, MatrixSrc4, MatrixDst4);
    }
    break;
   case 4: // matrix op 4x4 (1.0.15)
   case 6: // matrix op 4x4 (1.8.7)
    if(n >= 16)
    {
     int16 cmd[16];
     for(int i = 0; i < 16; i++) cmd[i] = S16(body[i]);
     MatrixOpNEC(cmd, MatrixSrc4, MatrixDst4, 4, option == 4 ? 15 : 7);
    }
    break;
   case 5: // matrix op 3x3 (1.0.15)
   case 7: // matrix op 3x3 (1.8.7)
    if(n >= 9)
    {
     int16 cmd[16] = { 0 };
     for(int r = 0; r < 3; r++)
      for(int c = 0; c < 3; c++)
       cmd[r * 4 + c] = S16(body[r * 3 + c]);
     MatrixOpNEC(cmd, MatrixSrc4, MatrixDst4, 3, option == 5 ? 15 : 7);
    }
    break;
   case 8: // destination -> source
    memcpy(MatrixSrc4, MatrixDst4, sizeof(MatrixSrc4));
    CopyMatrixToTESource(MatrixSrc4);
    break;
   case 9: // destination -> object
    CopyMatrixToTEObject(MatrixDst4);
    break;
   case 0xA: // destination -> normal (3x3)
   {
    const uint8 a[9]  = {56,58,60,62,64,66,68,70,72};
    for(int i = 0; i < 9; i++)
     TE[a[i]] = (uint16)MatrixDst4[(i / 3) * 4 + (i % 3)];
    break;
   }
   case 0xB: // destination -> result
    for(int i = 0; i < 16; i++)
     Results[i] = (uint16)MatrixDst4[i];
    break;
   case 0xC: // source[6] -> result[0..5]
    for(int i = 0; i < 6; i++)
     Results[i] = (uint16)MatrixSrc4[i];
    break;
   case 0xD: // result -> source
    for(int i = 0; i < 16; i++)
     MatrixSrc4[i] = S16(Results[i]);
    CopyMatrixToTESource(MatrixSrc4);
    break;
   case 0xE: // destination vector -> light1 vector
   case 0xF: // destination vector -> light2 vector
   {
    const uint8 l1[3] = {74,76,78}, l2[3] = {80,82,84};
    const uint8* a = (option == 0xE) ? l1 : l2;
    for(int i = 0; i < 3; i++)
     TE[a[i]] = (uint16)MatrixDst4[i * 4];
    break;
   }
  }
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
 { FILE* fp = TraceFP(); if(fp) { fprintf(fp, "CMD"); for(size_t i = 0; i < count; i++) fprintf(fp, " %04x", cmd[i]); fprintf(fp, "\n"); } }

 const uint16 h = cmd[0];
 const uint8 op = (h >> 12) & 0xF;
 const uint8 option = (h >> 8) & 0xF;
 const uint16* body = cmd + 1;
 const size_t n = count - 1;

 // NEC C6273_A: while a Vblank swap is pending, a PE frame control write
 // (dummy swap or another swap) or a command that touches buffer memory waits
 // for the swap.  Other PE register writes do not wait (they can overtake the
 // swap, which is the ordering problem the appendix describes).
 if(SwapPending && SwapPendingVsync && !PEStalled)
 {
  const bool waits = (op == OP_WRITE_PE_REGISTERS && (option & 0xF) == 5) ||
                     op == OP_TRIANGLE_STRIP || op == OP_TRIANGLE_LIST ||
                     op == OP_POLY_LINE || op == OP_LINE_LIST ||
                     op == OP_PUT_IMAGE || op == OP_READ_PIXEL ||
                     (op == OP_MISC && option == 0);
  if(waits)
   PEStalled = true;
 }

 switch(op)
 {
  case OP_NOP: Complete(INT_AEMP); break;
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
   if(PELockedUntilFrame)
   {
    DeferredPEReadPending = true;
    DeferredPEReadReg = option & 0xF;
   }
   else
   {
    ReadBack = PE[option & 0xF];
    Complete(INT_RBDONE);
   }
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

// AEMP is due once, when the FIFO has drained: replace any earlier pending one.
static void FifoEmptied(uint64 now)
{
 TimedPost(now);
 TimedCount[0] = 0;  // TimedBit[0] is INT_AEMP
 TimedSchedule(INT_AEMP, HucMax(TEFree, now), now);
}

// Run one command and post its completions when the hardware would have
// finished it (see "Command timing" above).
static void ProcessCommandTimed(const uint16* cmd, size_t count)
{
 if(!count || cmd[0] == 0xBEEF)
  return;
 const uint64 now = HucNow();
 const uint64 v0 = CntVerts, slow0 = CntPxSlow, fast0 = CntPxFast;
 const uint8 op = (cmd[0] >> 12) & 0xF;
 const uint8 option = (cmd[0] >> 8) & 0xF;

 InCommand = true;
 CmdBits = 0;
 ProcessCommand(cmd, count);
 InCommand = false;

 const uint64 te_start = HucMax(now, TEFree);
 // Lit vertices: TE control LTEN in I-C mode, on a primitive with normals
 // (vertex normal options 1, 5, 9; facet normal options 2, 6, A).
 const bool lit = (op == OP_TRIANGLE_STRIP || op == OP_TRIANGLE_LIST) &&
                  (option == 1 || option == 2 || option == 5 || option == 6 || option == 9 || option == 0xA) &&
                  (TE[255] & TE_CTRL_LTEN) && (TE[255] & TE_CTRL_ICM);
 const uint64 te_end = te_start + (CntVerts - v0) * (uint64)(lit ? HUC_CYC_PER_LIT_VERTEX : HUC_CYC_PER_VERTEX);
 TEFree = te_end;
 const bool draws = op == OP_TRIANGLE_STRIP || op == OP_TRIANGLE_LIST || op == OP_POLY_LINE ||
                    op == OP_LINE_LIST || op == OP_PUT_IMAGE || op == OP_READ_PIXEL ||
                    (op == OP_MISC && option == 0);
 if(draws)
  PEFree = HucMax(HucMax(PEFree, te_start) + PixelCycles(CntPxSlow - slow0, CntPxFast - fast0), te_end);
 const uint64 pe_done = HucMax(PEFree, te_end);
 const bool pe_read = op == OP_READ_PIXEL || op == OP_READ_PE_REGISTERS || op == OP_READ_LUT;

 uint16 bits = CmdBits;
 // NEC C6273_4 (interrupt status register): TESYNC is raised only by the TE
 // sync command, PESYNC only by a PE control register write with PES set
 // (bit 7).  The command handlers raise them more widely, which did no harm
 // while everything finished at once; with timing, a late one from another
 // command would end a wait early.
 if(!(op == OP_MISC && option == 2))
  bits &= ~INT_TESYNC;
 if(!(op == OP_WRITE_PE_REGISTERS && option == 3 && count > 1 && (cmd[1] & 0x0080)))
  bits &= ~INT_PESYNC;
 if(PEStalled)
 {
  HeldPEBits |= bits & INT_PESYNC;
  bits &= ~INT_PESYNC;
 }
 TimedSchedule(bits & INT_PESYNC, pe_done, now);
 TimedSchedule(bits & INT_RBDONE, pe_read ? pe_done : te_end, now);
 TimedSchedule(bits & ~(INT_PESYNC | INT_RBDONE | INT_AEMP), te_end, now);

 // FIFO: the command's words wait for the TE.  AEMP (with the default
 // AEMPWD of 32: the FIFO is empty) is due when the TE has taken everything
 // queued so far; AFL when the free words fall to AFLWD.
 if(te_end > now)
  FifoQPush(te_start, te_end, (uint32)count);
 FifoEmptied(now);
 if(FifoAvailable(now) <= (uint32)(FIFOControl & 0xF))
  InterruptStatus |= INT_AFL;
}

static void DrainFIFO(void)
{
 for(;;)
 {
  if(PendingFIFOEmpty())
   return;
  if(PendingFIFO[0] == 0xBEEF)
  {
   PendingFIFOPopFrontN(1);
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
   PendingFIFOPopFrontN(1);
   FifoEmptied(HucNow());
   continue;
  }

  uint8 length = header & 0xFF;
  if(length == 0)
   length = 1;
  if(PendingFIFOCountLive < length)
   return;
  ProcessCommandTimed(&PendingFIFO[0], length);
  PendingFIFOPopFrontN(length);
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
  uint16 h = ReadTexBankWord(CMTBankSelect, pos & 0xFFFF);
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
   tmp[i] = ReadTexBankWord(CMTBankSelect, (pos + i) & 0xFFFF);
  ProcessCommandTimed(tmp, len);
  pos += len;
  remaining -= len;
 }
 TimedSchedule(INT_CMDONE, HucMax(HucMax(TEFree, PEFree), HucNow()), HucNow());
}

uint8 HuC6273_Read8(uint32 A)
{
 uint16 v = HuC6273_Read16(A & ~1);
 return (uint8)(v >> ((A & 1) * 8));
}

uint16 HuC6273_Read16(uint32 A)
{
 A &= 0xFFFFF;
 if(A >= 0x10000 && A <= 0x1FFFF)
 {
  const uint32 o = ((A - 0x10000) >> 1) & 0xFFFF;
  const int bank = CMTBankSelect & (TEX_BANKS - 1);
  return TextureValid[bank][o] ? Texture[bank][o] : CommandTextureMem[o];
 }
 if(A >= 0x20000 && A <= 0x3FFFF)
 {
  const uint32 o = ((A - 0x20000) >> 1) & 0xFFFF;
  const int bank = CMTBankSelect & (TEX_BANKS - 1);
  if(TextureUnpackedValid[bank][o])
   return ReadTexBankUnpackedWord(bank, o);
  return 0;
 }

 switch(A & ~1)
 {
  case 0x00000:
  case 0x00002: return FifoAvailable(HucNow()); // AFW: free FIFO words (see FifoAvailable)
  case 0x00004: return FIFOControl;
  case 0x00006: return CMTBankSelect;
  case 0x00008: return CMTStartAddress;
  case 0x0000A: return CMTByteCount;
  case 0x0000C: return InterruptMask;
  case 0x0000E: return 0;
  case 0x00010: TimedPost(HucNow()); return InterruptStatus;
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
  case 0x00028:
  {
   // Busy bits follow the command timing: PBSY 3, TBSY 4, SBSY 5, MBSY 6
   // (a Vblank swap is waiting), RZBSY 7.
   const uint64 now = HucNow();
   uint16 v = MiscStatus & ~((1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 11) | (1 << 12));
   if(PEFree > now) v |= (1 << 3) | (1 << 7);
   if(TEFree > now) v |= 1 << 4;
   if(SPFree > now) v |= 1 << 5;
   if(SwapPending && SwapPendingVsync) v |= 1 << 6;
   return v;
  }
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
 {
  { FILE* fp = TraceFP(); if(fp) fprintf(fp, "RDRES %d %04x\n", (int)((A >> 1) & 0xF), Results[(A >> 1) & 0xF]); }
  return Results[(A >> 1) & 0xF];
 }
 return 0;
}

void HuC6273_Write16(uint32 A, uint16 V)
{
 A &= 0xFFFFF;
 if(A >= 0x10000 && A <= 0x1FFFF)
 {
  const uint32 o = ((A - 0x10000) >> 1) & 0xFFFF;
  WriteTexBankWord(CMTBankSelect, o, V);
  CommandTextureMem[o] = V;
  return;
 }
 if(A >= 0x20000 && A <= 0x3FFFF)
 {
  const uint32 o = ((A - 0x20000) >> 1) & 0xFFFF;
  WriteTexBankUnpackedWord(CMTBankSelect, o, V);
  return;
 }

 switch(A & ~1)
 {
  case 0x00000:
  case 0x00002:
   PendingFIFOPush(V);
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
  case 0x0000E:
   InterruptStatus &= ~V;
   if(V & (INT_VSY | INT_VBL | INT_FSY))
    MiscStatus &= ~((1 << 9) | (1 << 10));
   break;
  case 0x00010: InterruptStatus = V; break;
  case 0x00012: ReadBack = V; break;
  case 0x00014: HorizontalTiming = V; break;
  case 0x00016: VerticalTiming = V; break;
  case 0x00018: SCTAddress = V; break;
  case 0x0001A:
  {
   // Sprites are drawn at the pixel rates (Z-compared ones are the slow kind).
   const uint64 now = HucNow();
   const uint64 slow0 = CntPxSlow, fast0 = CntPxFast;
   SpriteControl = V;
   ExecuteSprites();
   SPFree = HucMax(now, SPFree) + PixelCycles(CntPxSlow - slow0, CntPxFast - fast0);
   Complete(0);
   TimedSchedule(INT_SPDONE, SPFree, now);
   break;
  }
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
    PendingFIFOCountLive = 0;
    TimingIdle();
    InterruptStatus = INT_AEMP;
    MiscStatus = 0;
    ErrorStatus = 0x0011;
    ClearAuroraFrameStorage(true);
   }
   break;
  case 0x00030: TECodeControl = V; break;
  case 0x00032: TEAddressControl = V; break;
  case 0x0003C: RasterHit = V; break;
  case 0x00040: PixelEngineTest = V; break;
  case 0x00042: MemoryTest = V; break;
  default:
   if(A >= 0x00060 && A <= 0x0007E)
   {
    { FILE* fp = TraceFP(); if(fp) fprintf(fp, "WRRES %d %04x\n", (int)((A >> 1) & 0xF), V); }
    Results[(A >> 1) & 0xF] = V;
   }
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

 if(A >= 0x10000 && A <= 0x1FFFF)
 {
  const uint32 o = ((A - 0x10000) >> 1) & 0xFFFF;
  WriteTexBankWord(CMTBankSelect, o, hi);
  WriteTexBankWord(CMTBankSelect, o + 1, lo);
  CommandTextureMem[o] = hi;
  CommandTextureMem[(o + 1) & 0xFFFF] = lo;
  return;
 }

 if(A >= 0x20000 && A <= 0x3FFFF)
 {
  const uint32 o = ((A - 0x20000) >> 1) & 0xFFFF;
  WriteTexBankUnpackedWord(CMTBankSelect, o, hi);
  WriteTexBankUnpackedWord(CMTBankSelect, o + 1, lo);
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
 TimingIdle();
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
 ClearAuroraFrameStorage(true);
 memset(Texture, 0, sizeof(Texture));
 memset(TextureValid, 0, sizeof(TextureValid));
 memset(TextureUnpacked, 0, sizeof(TextureUnpacked));
 memset(TextureUnpackedValid, 0, sizeof(TextureUnpackedValid));
 MatrixIdentity(MatrixSrc4);
 MatrixIdentity(MatrixDst4);
 ObjectMatrixValid = false;
 FrontBuffer = 0;
 DrawBuffer = 0;
 TE[2] = TE[20] = TE[38] = 0x0080; // object matrix identity, 1.8.7
 TE[75] = 0x0080; TE[77] = 0x0080; TE[79] = 0xFF80; TE[81] = 0x0080;
 TE[90] = 0x0FFF;
 PendingFIFOCountLive = 0;
 LastSpriteSCTAddress = LastSpriteControl = 0;
 LastSpriteValid = false;
 ReplayingSprites = false;
}

int HuC6273_StateAction(StateMem *sm, int load, int data_only)
{
 uint32 PendingFIFOCount = 0;
 uint16 PendingFIFOData[0x200];
 memset(PendingFIFOData, 0, sizeof(PendingFIFOData));

 if(!load)
 {
  PendingFIFOCount = (uint32)(PendingFIFOCountLive < 0x200 ? PendingFIFOCountLive : 0x200);
  for(uint32 i = 0; i < PendingFIFOCount; i++)
   PendingFIFOData[i] = PendingFIFO[i];
 }

 SFORMAT StateRegs[] =
 {
  SFVAR(FIFOControl),
  SFVAR(CMTBankSelect),
  SFVAR(CMTStartAddress),
  SFVAR(CMTByteCount),
  SFVAR(InterruptMask),
  SFVAR(InterruptStatus),
  SFVAR(ReadBack),
  SFVAR(HorizontalTiming),
  SFVAR(VerticalTiming),
  SFVAR(SCTAddress),
  SFVAR(SpriteControl),
  SFARRAY16(CDResult, 2),
  SFARRAY16(SPWindowX, 2),
  SFARRAY16(SPWindowY, 2),
  SFVAR(MiscStatus),
  SFVAR(ErrorStatus),
  SFVAR(DisplayControl),
  SFVAR(StatusControl),
  SFVAR(RasterHit),
  SFVAR(TECodeControl),
  SFVAR(TEAddressControl),
  SFVAR(PixelEngineTest),
  SFVAR(MemoryTest),
  SFARRAY16(Results, 16),
  SFARRAY16(TE, 256),
  SFARRAY16(PE, 16),
  SFARRAY16(LUT, 256),
  SFARRAY16(MatrixSrc4, 16),
  SFARRAY16(MatrixDst4, 16),
  SFVAR(ObjectMatrixValid),
  SFARRAY16(CommandTextureMem, 0x10000),
  SFARRAY(&FrameBuffer[0][0], sizeof(FrameBuffer)),
  SFARRAY(&FrameValid[0][0], 3 * FB_W * FB_H),
  SFARRAY32(ZBuffer, FB_W * FB_H),
  SFARRAY16(&Texture[0][0], TEX_BANKS * TEX_W * TEX_H),
  SFARRAY(&TextureValid[0][0], TEX_BANKS * TEX_W * TEX_H),
  SFARRAY16(&TextureUnpacked[0][0], TEX_BANKS * TEX_W * TEX_H),
  SFARRAY(&TextureUnpackedValid[0][0], TEX_BANKS * TEX_W * TEX_H),
  SFVAR(FrontBuffer),
  SFVAR(DrawBuffer),
  SFVAR(PendingBuffer),
  SFVAR(SwapPending),
  SFVAR(SwapPendingVsync),
  SFVAR(PEStalled),
  SFVAR(HeldPEBits),
  SFVAR(TimeBase),
  SFVAR(TimeNowTS),
  SFVAR(TEFree),
  SFVAR(PEFree),
  SFVAR(SPFree),
  SFVAR(SwapReadyTime),
  SFARRAY64(&TimedQ[0][0], TIMED_BITS * TIMED_QLEN),
  SFARRAY32(TimedHead, TIMED_BITS),
  SFARRAY32(TimedCount, TIMED_BITS),
  SFARRAY64(FifoQStart, FIFOQ_LEN),
  SFARRAY64(FifoQEnd, FIFOQ_LEN),
  SFARRAY32(FifoQWords, FIFOQ_LEN),
  SFVAR(FifoQHead),
  SFVAR(FifoQCount),
  SFVAR(PELockedUntilFrame),
  SFVAR(PELockFramesRemaining),
  SFVAR(DeferredPEReadPending),
  SFVAR(DeferredPEReadReg),
  SFVAR(PendingFIFOCount),
  SFARRAY16(PendingFIFOData, 0x200),
  SFEND
 };

 int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "HUC3", true);
 if(load)
 {
  if(FrontBuffer < 0 || FrontBuffer > 2) FrontBuffer = 0;
  if(DrawBuffer < 0 || DrawBuffer > 2 || DrawBuffer == FrontBuffer) DrawBuffer = (FrontBuffer + 1) % 3;
  if(PendingBuffer < 0 || PendingBuffer > 2 || PendingBuffer == FrontBuffer || PendingBuffer == DrawBuffer) PendingBuffer = 3 - FrontBuffer - DrawBuffer;
  if(PELockFramesRemaining > 120) PELockFramesRemaining = 0;
  if(!PELockedUntilFrame) PELockFramesRemaining = 0;
  DeferredPEReadReg &= 0x0F;
  if(!SwapPending) { SwapPendingVsync = false; PEStalled = false; HeldPEBits = 0; }
  HeldPEBits &= INT_PESYNC;
  for(int b = 0; b < TIMED_BITS; b++)
  {
   TimedHead[b] %= TIMED_QLEN;
   if(TimedCount[b] > TIMED_QLEN) TimedCount[b] = TIMED_QLEN;
  }
  InCommand = false;
  FifoQHead %= FIFOQ_LEN;
  if(FifoQCount > FIFOQ_LEN) FifoQCount = FIFOQ_LEN;
  PendingFIFOCountLive = 0;
  if(PendingFIFOCount > 0x200) PendingFIFOCount = 0x200;
  for(uint32 i = 0; i < PendingFIFOCount; i++)
   PendingFIFOPush(PendingFIFOData[i]);
 }
 return ret;
}

bool HuC6273_Init(void)
{
 HuC6273_Reset();
 return TRUE;
}

bool HuC6273_LineHasPixels(int y)
{
 if(y < 0 || y >= FB_H)
  return false;
 const uint8* valid = FrameValid[FrontBuffer] + y * FB_W;
 for(int x = 0; x < FB_W; x++)
  if(valid[x])
   return true;
 return false;
}

static inline int HuC6273_MapDisplayX(int x, int width)
{
 int sx = (width == FB_W) ? x : (x * FB_W + width / 2) / width;
 if(sx < 0) sx = 0;
 if(sx >= FB_W) sx = FB_W - 1;
 return sx;
}

void HuC6273_RenderLine(MDFN_Pixel* target, int y, int width)
{
 if(y < 0 || y >= FB_H || !target || width <= 0)
  return;

 const MDFN_Pixel* src = FrameBuffer[FrontBuffer] + y * FB_W;
 const uint8* valid = FrameValid[FrontBuffer] + y * FB_W;

 // The PC-FXGA Aurora frame is 256 pixels wide, but the host PC-FX video
 // compositor/frontends can expose wider active lines.  Scale the Aurora
 // display buffer across the current target width.
 for(int x = 0; x < width; x++)
 {
  const int sx = HuC6273_MapDisplayX(x, width);
  if(valid[sx])
   target[x] = src[sx];
 }
}

void HuC6273_RenderLinePriority(MDFN_Pixel* target, int y, int width, const uint8* vce_top_prio, uint8 aurora_prio)
{
 (void)aurora_prio;
 if(y < 0 || y >= FB_H || !target || !vce_top_prio || width <= 0)
  return;

 const MDFN_Pixel* src = FrameBuffer[FrontBuffer] + y * FB_W;
 const uint8* valid = FrameValid[FrontBuffer] + y * FB_W;

 for(int x = 0; x < width; x++)
 {
  const int sx = HuC6273_MapDisplayX(x, width);
  const uint8 pv = valid[sx];
  if(!pv)
   continue;

  if(pv == HUC6273_PIX_CLEAR)
  {
   // A PE clear with D=0 is the Aurora clear plane, not a sprite/primitive.
   // Let active KING/VDC/Rainbow pixels remain visible through it, but use it
   // to replace the otherwise-uninitialized host/default background.
   if(vce_top_prio[x] == 0)
    target[x] = src[sx];
  }
  else
   target[x] = src[sx];
 }
}
