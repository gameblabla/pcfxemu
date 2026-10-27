#include "sdl3/pcfx_interlace.h"
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
  uint8_t src[8] = {11,11,22,22,33,33,44,44}, dst[8];
  pcfx_bob_field(dst, src, 2, 4, 1);
  assert(dst[0]==11 && dst[2]==11 && dst[4]==33 && dst[6]==33);
  pcfx_bob_field(dst, src, 2, 4, 0);
  assert(dst[0]==22 && dst[2]==22 && dst[4]==44 && dst[6]==44);
  puts("bob field parity and duplication: PASS");
}
