/* Physical CD-ROM backend for pcfxemu's C11 CDIF layer.
 *
 * This is a small native MMC/SCSI pass-through backend modelled on Mednafen's
 * CDAccess_Physical class, but kept in C11 and without libcdio.  It supports
 * Linux SG_IO and Win32 IOCTL_SCSI_PASS_THROUGH_DIRECT.  Paths are selected
 * with a synthetic URI-like name so normal image paths remain unambiguous:
 *
 *   cdrom:              first detected OS CD-ROM drive
 *   cdrom:/dev/sr0      explicit Linux device
 *   cdrom:D:            explicit Windows drive letter
 *   cdrom:\\.\D:        explicit Windows device path
 */
#ifndef MDFN_CDROM_PHYS_H
#define MDFN_CDROM_PHYS_H

#include <stdbool.h>
#include <stdint.h>
#include "CDUtility.h"

typedef struct CDPhys CDPhys;

bool CDPhys_IsPath(const char *path);
CDPhys *CDPhys_Open(const char *path, TOC *out_toc);
void CDPhys_Close(CDPhys *p);
bool CDPhys_ReadRawSector(CDPhys *p, uint8_t *buf, int32_t lba);
const char *CDPhys_LastError(void);

#endif
