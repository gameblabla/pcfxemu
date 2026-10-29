#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mednafen/pcfx/soundbox.c"
static unsigned fetches[2];
v810_timestamp_t KING_Update(const v810_timestamp_t t) { return t + 1000; }
void PCFX_SetEvent(const int type, const v810_timestamp_t timestamp) {(void)type;(void)timestamp;}
void PCE_PSG_Write(PCE_PSG *p, int32 timestamp, uint8 address, uint8 value)
{(void)p;(void)timestamp;(void)address;(void)value;}
void SCSICD_SetCDDAVolume(float l,float r) {(void)l;(void)r;}
uint16 KING_GetADPCMHalfWord(int ch) { return (uint16)(0x3210 + (fetches[ch]++ << 8) + ch); }
void KING_RequestADPCMHalfWord(int ch) { SoundBox_ADPCMReceiveWord(ch, KING_GetADPCMHalfWord(ch)); }
void PCFX_RawAudio_AddDelta(PCFX_RawAudioMixer *m, uint32 t, int32 l, int32 r)
{(void)m;(void)t;(void)l;(void)r;}
int main(void)
{
 memset(&psg,0,sizeof(psg));
 psg.bigdiv = 1365;
 psg.smalldiv = 1;
 SoundBox_SetKINGADPCMControl(3);  /* both ch, 31.47 kHz */
 assert(fetches[0] == 0 && fetches[1] == 0);
 SoundBox_ADPCMTransferHSync(); /* first common cycle starts on -HSYNC */
 assert(fetches[0] == 1 && fetches[1] == 1);
 assert(psg.ADPCMInputValid[0] && psg.ADPCMInputValid[1]);
 SoundBox_ADPCMTransferHSync();
 assert(fetches[0] == 1 && fetches[1] == 1);
 SoundBox_ADPCMTransferHSync(); /* 2-HSYNC period at fastest rate */
 assert(fetches[0] == 2 && fetches[1] == 2);
 /* Last captured two bytes replace older unused bytes. */
 assert(psg.ADPCMInputWord[0] == 0x3310);
 SoundBox_ADPCMUpdate(700);
 assert(fetches[0] == 2 && fetches[1] == 2); /* no KRAM fetch by nibble decoder */
 assert(psg.ADPCMHalfWord[0] == 0x3310);
 assert(!psg.ADPCMInputValid[0]);
 assert(psg.ADPCMWhichNibble[0] == 4);
 /* Disabling one channel does not rephase the other. */
 SoundBox_SetKINGADPCMControl(2);
 SoundBox_ADPCMTransferHSync();
 SoundBox_ADPCMTransferHSync();
 assert(fetches[0] == 2 && fetches[1] == 3);
 /* Both channels share a slower cycle when KING rate bits request one. */
 SoundBox_SetKINGADPCMControl(0x6);
 for(int i=0;i<3;i++) SoundBox_ADPCMTransferHSync();
 assert(fetches[1] == 3);
 SoundBox_ADPCMTransferHSync();
 assert(fetches[1] == 4);
 /* A simultaneous R10 reset re-establishes the common HSYNC phase. */
 psg.ADPCMHaveDelta[0] = 9;
 psg.ADPCMDelta[0] = 123;
 psg.ADPCMHaveHalfWord[0] = true;
 psg.ADPCMWhichNibble[0] = 8;
 psg.ADPCMPendingControl = 0x30;
 psg.ADPCMPendingValid = true;
 SoundBox_HSync(710);
 assert(!psg.ADPCMTransferEstablished);
 assert(!psg.ADPCMInputValid[0] && !psg.ADPCMInputValid[1]);
 assert(psg.ADPCMHaveDelta[0] == 0 && psg.ADPCMDelta[0] == 0);
 assert(!psg.ADPCMHaveHalfWord[0] && psg.ADPCMWhichNibble[0] == 0);
 SoundBox_ADPCMTransferHSync();
 assert(!psg.ADPCMTransferEstablished); /* reset is still asserted */
 psg.ADPCMPendingControl = 0;
 psg.ADPCMPendingValid = true;
 SoundBox_HSync(730);
 SoundBox_ADPCMTransferHSync();
 assert(psg.ADPCMTransferEstablished);
 puts("HuC6230 shared HSYNC capture, last-word latch, rate and reset: PASS");
 return 0;
}
