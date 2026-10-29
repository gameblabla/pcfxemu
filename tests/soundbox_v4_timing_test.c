#include <assert.h>
#include <math.h>
#include <stdio.h>
/* Source inclusion gives access to the internal control latch/envelope state. */
#include "../mednafen/pcfx/soundbox.c"
static float last_l, last_r;
static int gfx_sync_count;
v810_timestamp_t KING_Update(const v810_timestamp_t t)
{ gfx_sync_count++; return t + 1000; }
void PCFX_SetEvent(const int type, const v810_timestamp_t timestamp)
{ (void)type; (void)timestamp; }
void PCE_PSG_Write(PCE_PSG *p, int32 timestamp, uint8 address, uint8 value) { (void)p; (void)timestamp; (void)address; (void)value; }
void SCSICD_SetCDDAVolume(float l, float r) { last_l=l; last_r=r; }
uint16 KING_GetADPCMHalfWord(int ch) { (void)ch; return 0; }
void KING_RequestADPCMHalfWord(int ch) { SoundBox_ADPCMReceiveWord(ch, KING_GetADPCMHalfWord(ch)); }
void PCFX_RawAudio_AddDelta(PCFX_RawAudioMixer *m, uint32 t, int32 l, int32 r)
{ (void)m; (void)t; (void)l; (void)r; }
int main(void)
{
 psg.bigdiv=1365;
 psg.smalldiv=1;
 SoundBox_Write(0x20, 0x10, 10);
 assert(psg.ADPCMControl==0 && psg.ADPCMPendingValid);
 assert(gfx_sync_count > 0);
 SoundBox_Write(0x20, 0x30, 20); /* last pre-HSync write wins */
 assert(psg.ADPCMControl==0);
 psg.ADPCMPredictor[0]=100;
 psg.ADPCMPredictor[1]=200;
 SoundBox_HSync(100);
 assert(psg.ADPCMControl==0x30 && !psg.ADPCMPendingValid);
 assert(psg.ADPCMPredictor[0]==0 && psg.ADPCMPredictor[1]==0);
 SoundBox_Write(0x20,0,120);
 assert(psg.ADPCMControl==0x30);
 SoundBox_HSync(300);
 assert(psg.ADPCMControl==0);

 /* HuC6230 PCM electronic-volume path must step rather than jump. */
 psg.CDDACurrentLevel[0]=1023;
 psg.CDDACurrentLevel[1]=1023;
 psg.CDDAVolume[0]=psg.CDDAVolume[1]=63;
 SoundBox_Write(0x2A, 0, 300);
 assert(psg.CDDACurrentLevel[0] == 1023);
 assert(psg.CDDAVolume[0] == 0);
 SoundBox_HSync(330);
 assert(psg.CDDACurrentLevel[0] == 1023);
 SoundBox_HSync(80000);
 assert(psg.CDDACurrentLevel[0] < 40);
 assert(psg.CDDACurrentLevel[0] > 0);
 assert(last_l < last_r);
 SoundBox_HSync(180000);
 assert(psg.CDDACurrentLevel[0] == 0);
 assert(last_l == 0);
 puts("HuC6230 HSYNC latch, coalesced R10 writes and stepped CDDA VCA: PASS");
 return 0;
}
