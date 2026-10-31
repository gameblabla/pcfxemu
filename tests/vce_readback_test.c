/* HuC6261 register readback regression (commit 6a49317 made every register read 0xFFFF).
 *
 * Software does read-modify-write on these registers.  Team Innocent clears the layer-enable bits of R00 one at a
 * time starting from a READ of R00: with an open-bus 0xFFFF the sequence started from "all layers on", so the VDC
 * "PUSH RUN BUTTON" prompt and background layers stayed visible on top of the FMV (doubled text, wrong picture).
 * Uses the actual KING source. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"
void SoundBox_SetKINGADPCMControl(uint32 value) {(void)value;}

static void vce_write(unsigned reg, uint16 v)
{
 FXVCE_Write16(0x0, reg);	/* A bit 2 clear: register select */
 FXVCE_Write16(0x4, v);
}

static uint16 vce_read(unsigned reg)
{
 FXVCE_Write16(0x0, reg);
 return FXVCE_Read16(0x4);
}

int main(void)
{
 /* R00 picture mode: the exact read-modify-write Team Innocent performs. */
 vce_write(0x00, 0x4700);
 assert(vce_read(0x00) == 0x4700);
 {
  uint16 cr = vce_read(0x00);
  cr &= (uint16)~0x0100u;			/* clear one layer-enable bit */
  vce_write(0x00, cr);
  assert(vce_read(0x00) == 0x4600);
  assert(fx_vce.picture_mode == 0x4600);
 }

 /* The other stored registers. */
 vce_write(0x08, 0x0123); assert(vce_read(0x08) == 0x0123);	/* priority 0 */
 vce_write(0x09, 0x1234); assert(vce_read(0x09) == 0x1234);	/* priority 1 */
 vce_write(0x0e, 0x00A5); assert(vce_read(0x0e) == 0x00A5);	/* BLE */
 vce_write(0x10, 0x0F0F); assert(vce_read(0x10) == 0x0F0F);	/* coefficient 0 */

 /* R03 is the palette read port: writing the offset in R01 then reading R03 yields the stored entry and advances. */
 fx_vce.palette_table[5] = 0x1234; fx_vce.palette_table[6] = 0x5678;
 vce_write(0x01, 5);
 assert(vce_read(0x01) == 5);
 fx_vce.palette_rw_latch = fx_vce.palette_table[5];
 assert(vce_read(0x03) == 0x1234);
 assert(vce_read(0x03) == 0x5678);

 /* R02 is write-only (documentation): open bus. */
 assert(vce_read(0x02) == 0xFFFF);

 puts("PASS vce_readback_test");
 return 0;
}
