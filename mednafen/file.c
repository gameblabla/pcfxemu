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
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>


#include "file.h"
#include "mednafen-endian.h"

static int read_entire_file(const char *path, uint8_t **out_data, int64_t *out_size)
{
   FILE *fp = NULL;
   long len = 0;
   uint8_t *buf = NULL;

   if (!path || !out_data || !out_size)
      return 0;

   *out_data = NULL;
   *out_size = 0;

   fp = fopen(path, "rb");
   if (!fp)
      return 0;

   if (fseek(fp, 0, SEEK_END) != 0)
      goto error;
   len = ftell(fp);
   if (len < 0)
      goto error;
   if (fseek(fp, 0, SEEK_SET) != 0)
      goto error;

   buf = (uint8_t*)malloc((size_t)len ? (size_t)len : 1);
   if (!buf)
      goto error;

   if (len && fread(buf, 1, (size_t)len, fp) != (size_t)len)
      goto error;

   fclose(fp);
   *out_data = buf;
   *out_size = (int64_t)len;
   return 1;

error:
   if (fp)
      fclose(fp);
   free(buf);
   return 0;
}

struct MDFNFILE *file_open(const char *path)
{
   int64_t size          = 0;
   const char        *ld = NULL;
   struct MDFNFILE *file = (struct MDFNFILE*)calloc(1, sizeof(*file));

   if (!file)
      return NULL;

   if (!read_entire_file(path, &file->data, &size))
      goto error;

   ld          = (const char*)strrchr(path, '.');
   file->size  = size;
   file->ext   = strdup(ld ? ld + 1 : "");

   return file;

error:
   if (file)
      free(file);
   return NULL;
}

int file_close(struct MDFNFILE *file)
{
   if (!file)
      return 0;

   if (file->ext)
      free(file->ext);
   file->ext = NULL;

   if (file->data)
      free(file->data);
   file->data = NULL;

   free(file);

   return 1;
}
