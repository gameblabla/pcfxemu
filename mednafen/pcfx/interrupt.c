/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <stdlib.h>
#include "pcfx.h"
#include "interrupt.h"
#include "../state_helpers.h"

static uint16 InterruptAsserted;
static uint16 InterruptMask;
static uint16 InterruptPriority[2];

static void BuildInterruptCache(void)
{
   uint32 iwithmask = InterruptAsserted &~ InterruptMask;
   int InterruptCache = -1;
   int last_prio = -1;

   for(int level = 8; level < 16; level++)
      if(iwithmask & (1 << (15 - level)))
      {
         int tmp_prio;

         if(level >= 12)
            tmp_prio = (InterruptPriority[0] >> ((15 - level) * 3)) & 0x7;
         else
            tmp_prio = (InterruptPriority[1] >> ((11 - level) * 3)) & 0x7;

         if(tmp_prio >= last_prio)
         {
            /*if(tmp_prio == last_prio)
            {
               FXDBG("Undefined IRQ behavior: %d, %d\n", level, tmp_prio);
            }*/
            InterruptCache = 8 + tmp_prio;
            last_prio = tmp_prio;
         }
      }

   V810_SetInt(InterruptCache);
}

void PCFXIRQ_Assert(int source, bool assert)
{
   //assert(source >= 0 && source <= 7);

   { static int tr = -1; static unsigned long n; if(tr < 0) { const char *e = getenv("PCFXEMU_IRQ_TRACE"); tr = (e && *e == '1') ? 1 : ((e && *e == '2') ? 2 : 0); }
     if(tr == 1 && assert && n++ < 40) fprintf(stderr, "[IRQSRC] assert src %d mask %04x prio %04x/%04x\n", source, InterruptMask, InterruptPriority[0], InterruptPriority[1]);
     if(tr == 2 && (source == 4 || source == 5 || source == 6) && n++ >= (unsigned long)(getenv("PCFXEMU_IRQ_TRACE_FROM") ? atol(getenv("PCFXEMU_IRQ_TRACE_FROM")) : 0) && n < 100000) fprintf(stderr, "[IRQSRC] src %d %s mask %04x\n", source, assert ? "ASSERT" : "clear", InterruptMask); }
   InterruptAsserted &= ~(1 << (7 - source));

   if(assert)
      InterruptAsserted |= (1 << (7 - source));

   BuildInterruptCache();
}


uint16 PCFXIRQ_Read16(uint32 A)
{
   switch(A & 0xC0)
   {
      case 0x00:
         return InterruptAsserted;
      case 0x40:
         return InterruptMask;
      case 0x80:
         return InterruptPriority[0];
      case 0xC0:
         return InterruptPriority[1];
   }

   return 0;
}

uint8 PCFXIRQ_Read8(uint32 A)
{
   return(PCFXIRQ_Read16(A&~1) >> ((A & 1) * 8));
}

void PCFXIRQ_Write16(uint32 A, uint16 V)
{
   // printf("IRQ Controller Write: %08x %04x\n", A, V);
   //
   switch(A & 0xC0)
   {
      case 0x00:
//         puts("Address error clear");
         break;
      case 0x40:
         InterruptMask = V & 0x7F;
         BuildInterruptCache();
         break;

      case 0x80:
         if(InterruptMask == 0x7F)
         {
            InterruptPriority[0] = V & 0xFFF;
            BuildInterruptCache();
         }
         break;

      case 0xC0:
         if(InterruptMask == 0x7F)
         {
            InterruptPriority[1] = V & 0x1FF;
            BuildInterruptCache();
         }
         break;
   }
}

int PCFXIRQ_StateAction(StateMem *sm, int load, int data_only)
{
   SFORMAT StateRegs[] =
   {
      SFVAR(InterruptAsserted),
      SFVAR(InterruptMask),
      SFARRAY16(InterruptPriority, 2),
      SFEND
   };

   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "IRQ", false);

   if(load)
      BuildInterruptCache();

   return(ret);
}


void PCFXIRQ_Reset(void)
{
   InterruptAsserted = 0;
   InterruptMask = 0xFFFF;

   InterruptPriority[0] = 0;
   InterruptPriority[1] = 0;

   BuildInterruptCache();
}

