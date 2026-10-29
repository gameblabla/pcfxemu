/* Focused HuC6261 / HuC6272 register tests, using the actual core source.
 * --gc-sections discards unrelated devices and their external dependencies. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"

static void select_vce(unsigned r) { FXVCE_Write16(0, (uint16)r); }
static void write_vce(unsigned r, uint16 v)
{
 select_vce(r);
 FXVCE_Write16(4, v);
}

int main(void)
{
 /* CPU write-side must not bypass the H-period render latch. */
 VCE_ResetRegisters();
 assert(fx_vce.SPBL == 0xFFFFu);
 assert(fx_vce.picture_mode == 0);
 memset(&vce_rendercache, 0, sizeof(vce_rendercache));
 write_vce(4, 0x1234);
 write_vce(0xA, 0x5080);
 assert(vce_rendercache.palette_offset[0] == 0);
 assert(vce_rendercache.ChromaKeyY == 0);
 DoHBlankVCECaching();
 assert(vce_rendercache.palette_offset[0] == 0x1234);
 assert(vce_rendercache.ChromaKeyY == 0x5080);
 write_vce(4, 0x5678);
 write_vce(0xA, 0x1070);
 assert(vce_rendercache.palette_offset[0] == 0x1234);
 assert(vce_rendercache.ChromaKeyY == 0x5080);
 DoHBlankVCECaching();
 assert(vce_rendercache.palette_offset[0] == 0x5678);
 assert(vce_rendercache.ChromaKeyY == 0x1070);

 /* Actual palette read port is CPR R03, not CPW R02. */
 write_vce(1, 0x1FE);
 write_vce(2, 0xA15A);
 write_vce(2, 0xB26B);
 write_vce(1, 0x1FE);
 select_vce(2);
 assert(FXVCE_Read16(4) == 0xFFFF); /* unspecified bus sentinel */
 assert(fx_vce.palette_rw_offset == 0x1FE);
 select_vce(3);
 assert(FXVCE_Read16(4) == 0xA15A);
 assert(FXVCE_Read16(4) == 0xB26B);
 assert(fx_vce.palette_rw_offset == 0);

 /* 1M KRAM mode masks the physical page, without rewriting PageSetting. */
 static king_t test_king;
 king = &test_king;
 king->PageSetting = 0x1111;
 king->KRAM_Mode = 0;
 assert(KING_KRAMPage(1) == 0);
 RecalcKRAMPagePtrs();
 assert(king->DMAPagePtr == king->KRAM[0]);
 assert(king->RainbowPagePtr == king->KRAM[0]);
 king->KRAM_Mode = 1;
 assert(KING_KRAMPage(1) == 1);
 RecalcKRAMPagePtrs();
 assert(king->DMAPagePtr == king->KRAM[1]);
 assert(king->RainbowPagePtr == king->KRAM[1]);
 assert(king->PageSetting == 0x1111);
#ifdef HAVE_HUC6273
 assert(sizeof(huc6273_vce_top_prio_line) == 1024);
#endif
 puts("KING/TETSU v4 register latch, palette read, 1M/4M page routing: PASS");
 return 0;
}

/* v23 test-harness hooks: display IDCT is not part of a K-BUS unit test. */
void IDCT(int32 *c) { (void)c; }
void RAINBOW_CompareLuma(const uint8 *luma, const uint8 *valid, uint32 *count, uint32 *diff)
{ (void)luma;(void)valid;(void)count;(void)diff; }

void j_rev_dct(int32 *c) { (void)c; }
bool RAINBOW_IsFastBackend(void) { return false; }
