#include "../mednafen-endian.h"

// The PC-FXGA BIOS exposes a small external PIO handshake in this mirrored
// address window.  Bully Off uses the BIOS helpers while starting up.
static uint8 PCFXGA_PIORegs[8];

static uint8 PCFXGA_PIORead8(uint32 A)
{
 return PCFXGA_PIORegs[A & 7];
}

static uint16 PCFXGA_PIORead16(uint32 A)
{
 return (uint16)(PCFXGA_PIORead8(A) | ((uint16)PCFXGA_PIORead8(A + 1) << 8));
}

static void PCFXGA_PIOWrite8(uint32 A, uint8 V)
{
 const uint32 o = A & 7;
 PCFXGA_PIORegs[o] = V;

 if(o == 6)
 {
  switch(V & 0x0F)
  {
   case 0x08: PCFXGA_PIORegs[4] |=  0x08; break;
   case 0x09: PCFXGA_PIORegs[4] &= ~0x08; break;
   case 0x0A: PCFXGA_PIORegs[4] &= ~0x04; break;
   case 0x0B: PCFXGA_PIORegs[4] |=  0x04; break;
  }
 }
}

static void PCFXGA_PIOWrite16(uint32 A, uint16 V)
{
 PCFXGA_PIOWrite8(A, (uint8)V);
 PCFXGA_PIOWrite8(A + 1, (uint8)(V >> 8));
}

static uint8 MDFN_FASTCALL mem_rbyte(v810_timestamp_t *timestamp, uint32 A)
{
 if(A <= 0x001FFFFF)
 {
  RAMLPCHECK;
  return(RAM[A]);
 }
 else if(A <= 0x00FFFFFF)
 {
  RAMLPCHECK;
  return(0xFF);
 }
 else if(A >= 0xF8000000 && A <= 0xFFEFFFFF)
 {
  (*timestamp) += 2;
  return(PCFXGA_PIORead8(A));
 }
 else if(A >= 0xF0000000)	// BIOS ROM mirrored throughout 0xF0000000-0xFFFFFFFF, the "official" location
				// is at 0xFFF00000(what about on a PC-FXGA??)
 {
  (*timestamp) += 2;
  return(BIOSROM[A & 0xFFFFF]);
 }
 else if(A >= 0xE0000000 && A <= 0xE7FFFFFF && !(A & 1))
 {

  //printf("%d\n", (A - 0xE0000000) >> 1);
  return(BackupRAM[(A & 0xFFFF) >> 1]);
 }
 else if(A >= 0xE8000000 && A <= 0xE9FFFFFF)
 {
  if(!(BackupControl & 0x2))
  {
   FXDBG("Read8 from external BRAM when not enabled.");
  }

  return(ExBackupRAM[(A & 0xFFFF) >> 1]);
 }
 else if(A >= 0x80000000 && A <= 0x807FFFFF)
 {
  //FXDBG("Mem->IO B Read Translation: %08x -> %08x", A, A & 0x7FFFFF);
  return(port_rbyte(timestamp, A & 0x7FFFFF));
 }
 FXDBG("Unknown byte read: %08x", A);
 return(0xFF);
}

static uint16 MDFN_FASTCALL mem_rhword(v810_timestamp_t *timestamp, uint32 A)
{
 if(A <= 0x001FFFFF)
 {
  RAMLPCHECK;
  return(le16toh(*(uint16*)&RAM[A]));
 }
 else if(A <= 0x00FFFFFF)
 {
  RAMLPCHECK;
  return(0xFFFF);
 }
 else if(A >= 0xF8000000 && A <= 0xFFEFFFFF)
 {
  (*timestamp) += 2;
  return(PCFXGA_PIORead16(A));
 }
 else if(A >= 0xF0000000)       // BIOS ROM mirrored throughout 0xF0000000-0xFFFFFFFF, the "official" location
                                // is at 0xFFF00000
 {
  (*timestamp) += 2;
  return(le16toh(*(uint16 *)&BIOSROM[A & 0xFFFFF]));
 }
 else if(A >= 0xA0000000 && A <= 0xA3FFFFFF)
 {
  (*timestamp) += 4;
  return(FXVCE_Read16(0x4));
 }
 else if(A >= 0xA4000000 && A <= 0xA7FFFFFF)
 {
  (*timestamp) += 4;
  return(VDC_Read16(fx_vdc_chips[0], 1, false));
 }
 else if(A >= 0xA8000000 && A <= 0xABFFFFFF)
 {
  (*timestamp) += 4;
  return(VDC_Read16(fx_vdc_chips[1], 1, false));
 }
 else if(A >= 0xAC000000 && A <= 0xAFFFFFFF)
 {
  (*timestamp) += 4;
  return(KING_Read16((*timestamp), 0x604));
 }
 else if(A >= 0xB0000000 && A <= 0xBFFFFFFF) // Write only
 {
  return(0);
 }
 else if(A >= 0xE0000000 && A <= 0xE7FFFFFF)
 {

  //printf("%d\n", (A - 0xE0000000) >> 1);
  return(BackupRAM[(A & 0xFFFF) >> 1]);
 }
 else if(A >= 0xE8000000 && A <= 0xE9FFFFFF)
 {
  if(!(BackupControl & 0x2))
  {
   FXDBG("Read16 from external BRAM when not enabled.");
  }

  return(ExBackupRAM[(A & 0xFFFF) >> 1]);
 }
 else if(A >= 0x80000000 && A <= 0x807FFFFF)
 {
  //FXDBG("Mem->IO H Read Translation: %08x -> %08x", A, A & 0x7FFFFF);

  return(port_rhword(timestamp, A & 0x7FFFFF));
 }
 FXDBG("Unknown hword read: %08x", A);

 return(0xFFFF);
}

static uint32 MDFN_FASTCALL mem_rword(v810_timestamp_t *timestamp, uint32 A)
{
 if(A <= 0x001FFFFF)
 {
  RAMLPCHECK;
  return(le32toh(*(uint32*)&RAM[A]));
 }
 else if(A <= 0x00FFFFFF)
 {
  RAMLPCHECK;
  return(0xFFFFFFFF);
 }
 else if(A >= 0xB0000000 && A <= 0xBFFFFFFF) // Write only
 {
  return(0);
 }
 else
 {
  uint32 ret;

  FXDBG("Warning:  mem_rword() called for 16-bit bus access: %08x", A);

  ret = mem_rhword(timestamp, A);
  ret |= mem_rhword(timestamp, A | 2) << 16;

  return(ret);
 }

 FXDBG("Unknown word read: %08x", A);

 return(0xFFFFFFFF);
}

static void MDFN_FASTCALL mem_wbyte(v810_timestamp_t *timestamp, uint32 A, uint8 V)
{
 if(A <= 0x001FFFFF)
 {
  RAMLPCHECK;
  RAM[A] = V;
 }
 else if(A <= 0x00FFFFFF)
 {
  RAMLPCHECK;
 }
 else if(A >= 0xF8000000 && A <= 0xFFEFFFFF)
 {
  (*timestamp) += 2;
  PCFXGA_PIOWrite8(A, V);
 }
 else if(A >= 0xE0000000 && A <= 0xE7FFFFFF && !(A & 1))
 {
  if(BackupControl & 0x1)
  {
   BackupRAM[(A & 0xFFFF) >> 1] = V;
  }
 }
 else if(A >= 0xE8000000 && A <= 0xE9FFFFFF)
 {
  //printf("ExWrite: %08x", A);
  if(BackupControl & 0x2)
  {
   ExBackupRAM[(A & 0xFFFF) >> 1] = V;
  }
 }
 else if(A >= 0x80000000 && A <= 0x807FFFFF)
 {
  //FXDBG("Mem->IO B Write Translation: %08x %02x -> %08x", A, V, A & 0x7FFFFF);
  port_wbyte(timestamp, A & 0x7FFFFF, V);
 }
}

static void MDFN_FASTCALL mem_whword(v810_timestamp_t *timestamp, uint32 A, uint16 V)
{
 if(A <= 0x001FFFFF)
 {
  RAMLPCHECK;
  *(uint16*)&RAM[A] = htole16(V);
 }
 else if(A <= 0x00FFFFFF)
 {
  RAMLPCHECK;
 }
 else if(A >= 0xF8000000 && A <= 0xFFEFFFFF)
 {
  (*timestamp) += 2;
  PCFXGA_PIOWrite16(A, V);
 }
 else if(A >= 0xE0000000 && A <= 0xE7FFFFFF)
 {

  if(BackupControl & 0x1)
  {
   BackupRAM[(A & 0xFFFF) >> 1] = (uint8)V;
  }
 }
 else if(A >= 0xE8000000 && A <= 0xE9FFFFFF)
 {
  //printf("ExWrite16: %08x", A);
  if(BackupControl & 0x2)
  {
   ExBackupRAM[(A & 0xFFFF) >> 1] = (uint8)V;
  }
 }
 else if(A >= 0xB0000000 && A <= 0xB3FFFFFF)
 {
  (*timestamp) += 2;
  FXVCE_Write16(0x4, V);
 }
 else if(A >= 0xB4000000 && A <= 0xB7FFFFFF)
 {
  (*timestamp) += 2;
  VDC_Write16(fx_vdc_chips[0], 1, V);
 }
 else if(A >= 0xB8000000 && A <= 0xBBFFFFFF)
 {
  (*timestamp) += 2;
  VDC_Write16(fx_vdc_chips[1], 1, V);
 }
 else if(A >= 0xBC000000 && A <= 0xBFFFFFFF)
 {
  (*timestamp) += 2;
  KING_Write16((*timestamp), 0x604, V);
  (*timestamp) += KING_KRAMWriteContentionCycles(0x604);
 }
 else if(A >= 0x80000000 && A <= 0x807FFFFF)
 {
  //FXDBG("Mem->IO H Write Translation: %08x %04x -> %08x", A, V, A & 0x7FFFFF);

  port_whword(timestamp, A & 0x7FFFFF, V);
 }
}

static void MDFN_FASTCALL mem_wword(v810_timestamp_t *timestamp, uint32 A, uint32 V)
{
#ifdef HAVE_HUC6273
 if(A >= 0x80500000 && A <= 0x8053FFFF)
 {
  if(WantHuC6273)
  {
   (*timestamp) += 4;
   HuC6273_SetTime(*timestamp);
   HuC6273_Write32(A & 0x7FFFFF, V);
   return;
  }
 }
#endif
 if(A <= 0x001FFFFF)
 {
  RAMLPCHECK;
  *(uint32*)&RAM[A] = htole32(V);
 }
 else if(A <= 0x00FFFFFF)
 {
  RAMLPCHECK;
 }
 else if(A >= 0xA0000000 && A <= 0xAFFFFFFF) // Read only
 {

 }
 else
 {
  FXDBG("Warning:  mem_wword() called for 16-bit bus access: %08x", A);
  mem_whword(timestamp, A, V);
  mem_whword(timestamp, A | 2, V >> 16);
 }
}
