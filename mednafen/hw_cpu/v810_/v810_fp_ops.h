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

#include "mednafen/mednafen.h"

extern unsigned exception_flags;


 uint32 V810_FP_Ops_mul(uint32 a, uint32 b);
 uint32 V810_FP_Ops_div(uint32 a, uint32 b);
 uint32 V810_FP_Ops_add(uint32 a, uint32 b);
 uint32 V810_FP_Ops_sub(uint32 a, uint32 b);
 int V810_FP_Ops_cmp(uint32 a, uint32 b);

 uint32 V810_FP_Ops_itof(uint32 v);
 uint32 V810_FP_Ops_ftoi(uint32 v, bool truncate);

 enum
 {
  flag_invalid = 0x0001,
  flag_divbyzero = 0x0002,
  flag_overflow = 0x0004,
  flag_underflow = 0x0008,
  flag_inexact = 0x0010,
  flag_reserved = 0x0020
 };

 inline uint32 V810_FP_Ops_get_flags(void)
 {
  return exception_flags;
 }

 inline void V810_FP_Ops_clear_flags(void)
 {
  exception_flags = 0;
 }

