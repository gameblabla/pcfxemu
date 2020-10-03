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

#include <errno.h>
#include <string.h>

#include <boolean.h>

#include "settings.h"

int setting_initial_scanline = 0;
int setting_last_scanline = 239;
int setting_high_dotclock_width = 1024;
int setting_nospritelimit = 0;
int setting_resamp_quality = 3;
int setting_suppress_channel_reset_clicks = 1;
int setting_emulate_buggy_codec = 0;
int setting_rainbow_chromaip = 0;

uint64_t MDFN_GetSettingUI(const char *name)
{
   if (!strcmp("pcfx.cdspeed", name))
      return 2;
   if (!strcmp("pcfx.slend", name))
      return setting_last_scanline;
   if (!strcmp("pcfx.slstart", name))
      return setting_initial_scanline;
   if (!strcmp("pcfx.high_dotclock_width", name))
      return setting_high_dotclock_width;
   if (!strcmp("pcfx.resamp_quality", name))
      return setting_resamp_quality;
   return 0;
}

int64_t MDFN_GetSettingI(const char *name)
{
   if (!strcmp("pcfx.cpu_emulation", name))
      return 2;
   return 0;
}

double MDFN_GetSettingF(const char *name)
{
   if (!strcmp("pcfx.resamp_rate_error", name))
      return 0.0000009;
   if (!strcmp("pcfx.mouse_sensitivity", name))
      return  	1.25; /* TODO - make configurable */
   return 0;
}

bool MDFN_GetSettingB(const char *name)
{
   if (!strcmp("cheats", name))
      return 0;
   /* LIBRETRO */
   if (!strcmp("libretro.cd_load_into_ram", name))
      return 0;
   if (!strcmp("pcfx.disable_softreset", name))
      return 0; /* TODO - make configurable */
   if (!strcmp("pcfx.input.port1.multitap", name))
      return 0; /* TODO - make configurable */
   if (!strcmp("pcfx.input.port2.multitap", name))
      return 0; /* TODO - make configurable */
   if (!strcmp("pcfx.nospritelimit", name))
      return setting_nospritelimit;
   if (!strcmp("pcfx.adpcm.suppress_channel_reset_clicks", name))
      return setting_suppress_channel_reset_clicks;
   if (!strcmp("pcfx.disable_bram", name))
      return 0; /* TODO - make configurable */
   if (!strcmp("pcfx.adpcm.emulate_buggy_codec", name))
      return setting_emulate_buggy_codec;
   if (!strcmp("pcfx.rainbow.chromaip", name))
      return setting_rainbow_chromaip;
   /* CDROM */
   if (!strcmp("cdrom.lec_eval", name))
      return 1;
   /* FILESYS */
   if (!strcmp("filesys.untrusted_fip_check", name))
      return 0;
   return 0;
}

const char *MDFN_GetSettingS(const char *name)
{
   if (!strcmp("pcfx.bios", name))
      return "pcfx.rom";
   if (!strcmp("pcfx.fxscsi", name))
      return "pcfx.fxscsi";
   return 0;
}
