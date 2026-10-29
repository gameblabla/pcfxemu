/* Check opt-in historical strip concealment without emulating a full decoder. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t uint32;
typedef int32_t int32;
#include "../mednafen/pcfx/rainbow_confetti.inc"
int main(int argc, char **argv)
{
 int legacy = argc > 1 && argv[1][0] == '1';
 uint32 original[256 * 16], test[256 * 16];
 for(int i = 0; i < 256 * 16; i++) original[i] = 0x008080;
 for(int i = 0; i < 256 * 16; i++) test[i] = 0x008080;
 RB_ConfettiReset();
 RB_ConfettiBeginFrame();
 RB_ConfettiAdvanceRow();
 RB_ConfettiBeginFrame();
 RB_ConfettiRepair(original, 0x008080, 0);
 for(int y = 0; y < 16; y++)
  for(int x = 0; x < 256; x++) test[y*256+x] = (x >= 64 && x < 80 && (x & 1)) ? 0xFF00FF : 0x008080;
 RB_ConfettiRepair(test, 0x008080, 0);
 if(legacy)
  assert(memcmp(test, original, sizeof(test)) == 0); /* heuristic destroys legitimate stripes */
 else
  for(int y = 0; y < 16; y++)
   for(int x = 0; x < 256; x++)
    assert(test[y*256+x] == ((x >= 64 && x < 80 && (x & 1)) ? 0xFF00FFu : 0x008080u));
 puts("RAINBOW concealment mode OK");
 return 0;
}
