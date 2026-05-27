#ifndef __PCE_VDC_H
#define __PCE_VDC_H

#include "mednafen/mednafen.h"
#include "mednafen/state.h"
#include "mednafen/state_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VDC_PIXEL_OUT_MASK      0x01FF
#define VDC_BGDISABLE_OUT_MASK  0x0200
#define VDC_HSYNC_OUT_MASK      0x2000
#define VDC_VSYNC_OUT_MASK      0x4000
#define VDC_DISP_OUT_MASK       0x8000

#define VDC_REGSETP(_reg, _data, _msb) do { (_reg) &= 0xFF << ((_msb) ? 0 : 8); (_reg) |= (uint16)((_data) << ((_msb) ? 8 : 0)); } while(0)
#define VDC_REGGETP(_reg, _msb) (((_reg) >> ((_msb) ? 8 : 0)) & 0xFF)

static const unsigned int vram_inc_tab[4] = { 1, 32, 64, 128 };

#define VDC_IS_BSY(vdc) ((vdc)->pending_read || (vdc)->pending_write)

typedef struct
{
        uint32 x;
        uint32 flags;
        uint8 palette_index;
        uint16 pattern_data[4];
} SPRLE;

typedef struct
{
        uint32 ReadStart;
        uint32 ReadCount;
        uint32 WriteStart;
        uint32 WriteCount;
} VDC_SimulateResult;

typedef struct VDC
{
        int VRAM_Size;
        int VRAM_SizeMask;
        int VRAM_BGTileNoMask;

        void (*IRQHook)(bool);
        bool (*WSHook)(int32);

        bool in_exhsync, in_exvsync;

        uint8 Simulate_select;
        uint16 Simulate_MAWR;
        uint16 Simulate_MARR;
        uint16 Simulate_CR;
        uint16 Simulate_LENR;

        int32 sat_dma_counter;

        uint8 select;
        uint16 MAWR;
        uint16 MARR;

        uint16 CR;
        uint16 CR_cache;
        uint16 RCR;
        uint16 BXR;
        uint16 BYR;
        uint16 MWR;

        uint16 HSR;
        uint16 HDR;
        uint16 VSR;
        uint16 VDR;

        uint16 VCR;
        uint16 DCR;
        uint16 SOUR;
        uint16 DESR;
        uint16 LENR;
        uint16 DVSSR;

        int32 VDMA_CycleCounter;
        uint32 RCRCount;

        bool pending_read;
        uint16 pending_read_addr;
        uint16 read_buffer;

        uint8 write_latch;

        bool pending_write;
        uint16 pending_write_addr;
        uint16 pending_write_latch;

        uint8 status;

        uint16 SAT[0x100];
        uint16 VRAM[65536];

        union
        {
         uint64 bg_tile_cache64[65536 / 16][8];
         uint8 bg_tile_cache[65536 / 16][8][8];
        } cache;

        uint16 DMAReadBuffer;
        bool DMAReadWrite;
        bool DMARunning;
        bool DMAPending;
        bool SATBPending;
        bool burst_mode;

        uint32 BG_YOffset;
        uint32 BG_XOffset;

        uint32 HSW_cache, HDS_cache, HDW_cache, HDE_cache;

        uint32 VDS_cache;
        uint32 VSW_cache;
        uint32 VDW_cache;
        uint32 VCR_cache;
        uint16 MWR_cache;

        uint32 BG_YMoo;
        bool NeedRCRInc, NeedVBIRQTest, NeedSATDMATest, NeedBGYInc;
        int HPhase, VPhase;
        int32 HPhaseCounter, VPhaseCounter;

        int32 sprite_cg_fetch_counter;
        int32 mystery_counter;
        bool mystery_phase;

        uint16 linebuf[1024 + 512];
        uint32 pixel_desu;
        int32 pixel_copy_count;
        uint32 userle;
        bool unlimited_sprites;

        int active_sprites;
        SPRLE SpriteList[64 * 2];
} VDC;

// Register enums for GetRegister() and SetRegister()
enum
{
        GSREG_MAWR = 0,
        GSREG_MARR,
        GSREG_CR,
        GSREG_RCR,
        GSREG_BXR,
        GSREG_BYR,
        GSREG_MWR,
        GSREG_HSR,
        GSREG_HDR,
        GSREG_VSR,
        GSREG_VDR,
        GSREG_VCR,
        GSREG_DCR,
        GSREG_SOUR,
        GSREG_DESR,
        GSREG_LENR,
        GSREG_DVSSR,
        GSREG_SELECT,
        GSREG_STATUS,
        __GSREG_COUNT
};

VDC *VDC_New(bool nospritelimit, uint32 par_VRAM_Size);
void VDC_Delete(VDC *vdc);
int32 VDC_Reset(VDC *vdc) MDFN_WARN_UNUSED_RESULT;
int32 VDC_HSync(VDC *vdc, bool hb);
int32 VDC_VSync(VDC *vdc, bool vb);
void VDC_Write(VDC *vdc, uint32 A, uint8 V);
uint8 VDC_Read(VDC *vdc, uint32 A, bool peek);
void VDC_Write16(VDC *vdc, bool A, uint16 V);
uint16 VDC_Read16(VDC *vdc, bool A, bool peek);
int32 VDC_Run(VDC *vdc, int32 clocks, uint16 *pixels, bool skip);
void VDC_FixTileCache(VDC *vdc, uint16 A);
void VDC_SetLayerEnableMask(VDC *vdc, uint64 mask);
void VDC_RunDMA(VDC *vdc, int32 cycles, bool force_completion);
void VDC_RunSATDMA(VDC *vdc, int32 cycles, bool force_completion);
void VDC_IncRCR(VDC *vdc);
void VDC_DoVBIRQTest(VDC *vdc);
void VDC_HDS_Start(VDC *vdc);
int VDC_StateAction(VDC *vdc, StateMem *sm, int load, int data_only, const char *sname);

static inline void VDC_ResetSimulate(VDC *vdc)
{
        vdc->Simulate_MAWR = vdc->MAWR;
        vdc->Simulate_MARR = vdc->MARR;
        vdc->Simulate_select = vdc->select;
        vdc->Simulate_CR = vdc->CR;
        vdc->Simulate_LENR = vdc->LENR;
}

static inline void VDC_SimulateRead(VDC *vdc, uint32 A, VDC_SimulateResult *result)
{
        result->ReadCount = 0;
        result->WriteCount = 0;
        result->ReadStart = 0;
        result->WriteStart = 0;
        if((A & 0x3) == 0x3 && vdc->Simulate_select == 0x02)
        {
                vdc->Simulate_MARR += vram_inc_tab[(vdc->Simulate_CR >> 11) & 0x3];
                result->ReadStart = vdc->Simulate_MARR;
                result->ReadCount = 1;
        }
}

static inline void VDC_SimulateWrite(VDC *vdc, uint32 A, uint8 V, VDC_SimulateResult *result)
{
        result->ReadCount = 0;
        result->WriteCount = 0;
        result->ReadStart = 0;
        result->WriteStart = 0;
        const unsigned int msb = A & 1;
        switch(A & 0x3)
        {
         case 0x00: vdc->Simulate_select = V & 0x1F; break;
         case 0x02:
         case 0x03:
          switch(vdc->Simulate_select)
          {
           case 0x00: VDC_REGSETP(vdc->Simulate_MAWR, V, msb); break;
           case 0x01:
            VDC_REGSETP(vdc->Simulate_MARR, V, msb);
            vdc->Simulate_MARR += vram_inc_tab[(vdc->Simulate_CR >> 11) & 0x3];
            result->ReadStart = vdc->Simulate_MARR;
            result->ReadCount = 1;
            break;
           case 0x02:
            if(msb)
            {
             result->WriteStart = vdc->Simulate_MAWR;
             result->WriteCount = 1;
             vdc->Simulate_MAWR += vram_inc_tab[(vdc->Simulate_CR >> 11) & 0x3];
            }
            break;
           case 0x12:
            VDC_REGSETP(vdc->Simulate_LENR, V, msb);
            if(msb)
            {
             result->ReadStart = vdc->SOUR;
             result->ReadCount = vdc->Simulate_LENR + 1;
             if(vdc->DCR & 0x4) result->ReadStart = (result->ReadStart - (result->ReadCount - 1)) & 0xFFFF;
             result->WriteStart = vdc->DESR;
             result->WriteCount = vdc->Simulate_LENR + 1;
             if(vdc->DCR & 0x8) result->WriteStart = (result->WriteStart - (result->WriteCount - 1)) & 0xFFFF;
            }
            break;
          }
          break;
        }
}

static inline void VDC_SimulateRead16(VDC *vdc, bool A, VDC_SimulateResult *result)
{
        result->ReadCount = 0;
        result->WriteCount = 0;
        result->ReadStart = 0;
        result->WriteStart = 0;
        if(A && vdc->Simulate_select == 0x02)
        {
                vdc->Simulate_MARR += vram_inc_tab[(vdc->Simulate_CR >> 11) & 0x3];
                result->ReadStart = vdc->Simulate_MARR;
                result->ReadCount = 1;
        }
}

static inline void VDC_SimulateWrite16(VDC *vdc, bool A, uint16 V, VDC_SimulateResult *result)
{
        result->ReadCount = 0;
        result->WriteCount = 0;
        result->ReadStart = 0;
        result->WriteStart = 0;
        if(!A)
                vdc->Simulate_select = V & 0x1F;
        else
        {
                switch(vdc->Simulate_select)
                {
                 case 0x00: vdc->Simulate_MAWR = V; break;
                 case 0x01:
                  vdc->Simulate_MARR = V;
                  vdc->Simulate_MARR += vram_inc_tab[(vdc->Simulate_CR >> 11) & 0x3];
                  result->ReadStart = vdc->Simulate_MARR;
                  result->ReadCount = 1;
                  break;
                 case 0x02:
                  result->WriteStart = vdc->Simulate_MAWR;
                  result->WriteCount = 1;
                  vdc->Simulate_MAWR += vram_inc_tab[(vdc->Simulate_CR >> 11) & 0x3];
                  break;
                 case 0x12:
                  vdc->Simulate_LENR = V;
                  result->ReadStart = vdc->SOUR;
                  result->ReadCount = vdc->Simulate_LENR + 1;
                  if(vdc->DCR & 0x4) result->ReadStart = (result->ReadStart - (result->ReadCount - 1)) & 0xFFFF;
                  result->WriteStart = vdc->DESR;
                  result->WriteCount = vdc->Simulate_LENR + 1;
                  if(vdc->DCR & 0x8) result->WriteStart = (result->WriteStart - (result->WriteCount - 1)) & 0xFFFF;
                  break;
                }
        }
}

static inline uint16 VDC_PeekVRAM(VDC *vdc, uint16 Address)
{
        return (Address < vdc->VRAM_Size) ? vdc->VRAM[Address] : 0;
}

static inline uint16 VDC_PeekSAT(VDC *vdc, uint8 Address)
{
        return vdc->SAT[Address];
}

static inline void VDC_PokeVRAM(VDC *vdc, uint16 Address, const uint16 Data)
{
        if(Address < vdc->VRAM_Size)
        {
                vdc->VRAM[Address] = Data;
                VDC_FixTileCache(vdc, Address);
        }
}

static inline void VDC_PokeSAT(VDC *vdc, uint8 Address, const uint16 Data)
{
        vdc->SAT[Address] = Data;
}

static inline bool VDC_PeekIRQ(VDC *vdc)
{
        return (bool)(vdc->status & 0x3F);
}

static inline void VDC_SetIRQHook(VDC *vdc, void (*irqh)(bool))
{
        vdc->IRQHook = irqh;
}

static inline void VDC_SetWSHook(VDC *vdc, bool (*wsh)(int32))
{
        vdc->WSHook = wsh;
}

static inline uint32 VDC_GetCachedDisplayWidth(const VDC *vdc)
{
        uint32 ret = (vdc->HDW_cache + 1) * 8;
        if(ret > 512) ret = 512;
        return ret;
}

#ifdef __cplusplus
}
#endif

#endif
