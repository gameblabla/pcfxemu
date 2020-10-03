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

#include <stdarg.h>
#include <string.h>

#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "mednafen.h"
#include "FileWrapper.h"
#include "Stream.h"

// For special uses, IE in classes that take a path or a FileWrapper & in the constructor, and the FileWrapper non-pointer member
// is in the initialization list for the path constructor but not the constructor with FileWrapper&

FileWrapper::FileWrapper(const char *path, const int mode, const char *purpose) : OpenedMode(mode)
{
 if(mode == MODE_WRITE)
  fp = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
 else
  fp = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);

 if(!fp)
 {
  ErrnoHolder ene(errno);

  throw(MDFN_Error(ene.Errno(), "Error opening file %s", ene.StrError()));
 }
}

FileWrapper::~FileWrapper()
{
   close();
}

void FileWrapper::close(void)
{
   if(!fp)
      return;

   RFILE *tmp = fp;
   fp = NULL;
   filestream_close(tmp);
}

uint64 FileWrapper::read(void *data, uint64 count, bool error_on_eof)
{
   return filestream_read(fp, data, count);
}

void FileWrapper::flush(void)
{
   filestream_flush(fp);
}

void FileWrapper::write(const void *data, uint64 count)
{
   filestream_write(fp, data, count);
}

void FileWrapper::put_char(int c)
{
   filestream_putc(fp, c);
}

void FileWrapper::put_string(const char *str)
{
   write(str, strlen(str));
}

// We need to decide whether to prohibit NULL characters in output and input strings via std::string.
// Yes for correctness, no for potential security issues(though unlikely in context all things considered).
void FileWrapper::put_string(const std::string &str)
{
   write(str.data(), str.size());
}

char *FileWrapper::get_line(char *buf_s, int buf_size)
{
   return filestream_gets(fp, buf_s, buf_size);
}


void FileWrapper::seek(int64 offset, int whence)
{
   filestream_seek(fp, offset, whence);
}

uint64_t FileWrapper::size(void)
{
   ssize_t curPos = filestream_tell(fp);
   filestream_seek(fp, 0, SEEK_END);
   ssize_t output = filestream_tell(fp);
   filestream_seek(fp, curPos, SEEK_SET);
   return(output);
}

uint64_t FileWrapper::tell(void)
{
   return filestream_tell(fp);
}
