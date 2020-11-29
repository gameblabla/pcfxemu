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

#include "../pcfx.h"
#include "../input.h"
#include "gamepad.h"

#include "../../mednafen-endian.h"
#include "../../state_helpers.h"

class PCFX_Input_Gamepad : public PCFX_Input_Device
{
 public:
 PCFX_Input_Gamepad(int which_param)
 {
  old_raw_buttons = 0;
  mode1 = false;
  mode2 = false;
  buttons = 0;
  which = which_param;
 }

 virtual ~PCFX_Input_Gamepad()
 {

 }

 virtual uint32 ReadTransferTime(void)
 {
  return(1536);
 }

 virtual uint32 WriteTransferTime(void)
 {
  return(1536);
 }

 virtual uint32 Read(void)
 {
  return(buttons | (FX_SIG_PAD << 28));
 }

 virtual void Write(uint32 data)
 {

 }


 virtual void Power(void)
 {
  buttons = 0;
 }

 virtual void Frame(const void *data)
 {
  uint16 new_buttons = MDFN_de16lsb((uint8 *)data);
  if((old_raw_buttons ^ new_buttons) & (1 << 12) & new_buttons)
  {
   mode1 = !mode1;
  }

  if((old_raw_buttons ^ new_buttons) & (1 << 14) & new_buttons)
  {
   mode2 = !mode2;
  }

  /*if(mode_changed)
   MDFN_DispMessage("Pad %d - MODE 1: %s, MODE 2: %s", which + 1, (mode1 ? "B" : "A"), (mode2 ? "B" : "A"));*/

  buttons = new_buttons & ~( (1 << 12) | (1 << 14));
  buttons |= mode1 << 12;
  buttons |= mode2 << 14;

  old_raw_buttons = new_buttons;
  //printf("%d %08x\n", which, buttons);
 }

 virtual int StateAction(StateMem *sm, int load, int data_only, const char *section_name)
 {
  SFORMAT StateRegs[] =
  {
   SFVAR(buttons),
   SFVAR(old_raw_buttons),
   SFVAR(mode1),
   SFVAR(mode2),
   SFEND
  };
  int ret =  MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name, false);

  return(ret);
 }

 private:

 uint16 buttons;
 uint16 old_raw_buttons;
 bool mode1;
 bool mode2;
 int which;
};

#if 0
int PCFX_Input_Gamepad::StateAction(StateMem *sm, int load, int data_only, const char *section_name)
{
 #if 0
 SFORMAT StateRegs[] =
 {
  SFEND
 };
 int ret =  MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name, false);

 return(ret);
 #endif
}
#endif


PCFX_Input_Device *PCFXINPUT_MakeGamepad(int which)
{
 return(new PCFX_Input_Gamepad(which));
}
