/* Mednafen-derived physical CD support for pcfxemu's C11 CDIF layer.
 *
 * The command sequence intentionally mirrors Mednafen's CDAccess_Physical:
 *   - MMC READ TOC/PMA/ATIP, format 2, session 1, to build a TOC.
 *   - MMC READ CD with 2352-byte main-channel data and raw P-W subchannel.
 *
 * Unlike Mednafen's original C++ implementation, this file talks directly to
 * the OS native pass-through APIs and has no libcdio dependency.
 */
#include "cdrom_phys.h"

#ifdef PCFX_ENABLE_PHYSICAL_CD
#include "../mednafen-endian.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

struct CDPhys
{
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
    TOC toc;
    char device[256];
};

static char cdphys_last_error[512];

const char *CDPhys_LastError(void)
{
    return cdphys_last_error[0] ? cdphys_last_error : "physical CD backend error";
}

static void set_last_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cdphys_last_error, sizeof(cdphys_last_error), fmt, ap);
    va_end(ap);
}

static bool starts_with_scheme(const char *path, const char *scheme)
{
    size_t n;
    if(!path || !scheme)
        return false;
    n = strlen(scheme);
#if defined(_WIN32)
    return _strnicmp(path, scheme, n) == 0;
#else
    return strncasecmp(path, scheme, n) == 0;
#endif
}

bool CDPhys_IsPath(const char *path)
{
    return starts_with_scheme(path, "cdrom:") ||
           starts_with_scheme(path, "physical-cd:") ||
           starts_with_scheme(path, "physcd:");
}

static const char *path_payload(const char *path)
{
    const char *colon;
    if(!path)
        return "";
    colon = strchr(path, ':');
    return colon ? colon + 1 : path;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_be24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

#ifdef _WIN32
static bool native_open_device(CDPhys *p, const char *path)
{
    char dev[256] = {0};
    const char *payload = path_payload(path);

    if(!payload[0])
    {
        DWORD drives = GetLogicalDrives();
        for(char letter = 'D'; letter <= 'Z'; letter++)
        {
            if(!(drives & (1u << (letter - 'A'))))
                continue;
            char root[] = { letter, ':', '\\', 0 };
            if(GetDriveTypeA(root) == DRIVE_CDROM)
            {
                snprintf(dev, sizeof(dev), "\\\\.\\%c:", letter);
                break;
            }
        }
        if(!dev[0])
        {
            set_last_error("no Win32 CD-ROM drive was detected");
            return false;
        }
    }
    else if((isalpha((unsigned char)payload[0]) && payload[1] == ':' && payload[2] == 0) ||
            (isalpha((unsigned char)payload[0]) && payload[1] == 0))
        snprintf(dev, sizeof(dev), "\\\\.\\%c:", (char)toupper((unsigned char)payload[0]));
    else
        snprintf(dev, sizeof(dev), "%s", payload);

    p->h = CreateFileA(dev, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(p->h == INVALID_HANDLE_VALUE)
    {
        set_last_error("failed to open physical CD device '%s' (Win32 error %lu)", dev, (unsigned long)GetLastError());
        return false;
    }
    snprintf(p->device, sizeof(p->device), "%s", dev);
    return true;
}

static void native_close_device(CDPhys *p)
{
    if(p && p->h != INVALID_HANDLE_VALUE)
    {
        CloseHandle(p->h);
        p->h = INVALID_HANDLE_VALUE;
    }
}

static bool native_scsi_read(CDPhys *p, const uint8_t *cdb, uint8_t cdb_len, void *data, uint32_t data_len, unsigned timeout_sec)
{
    typedef struct SPTDWB
    {
        SCSI_PASS_THROUGH_DIRECT sptd;
        ULONG filler;
        UCHAR sense[32];
    } SPTDWB;

    SPTDWB wb;
    DWORD returned = 0;
    memset(&wb, 0, sizeof(wb));
    wb.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    wb.sptd.CdbLength = cdb_len;
    wb.sptd.SenseInfoLength = sizeof(wb.sense);
    wb.sptd.DataIn = SCSI_IOCTL_DATA_IN;
    wb.sptd.DataTransferLength = data_len;
    wb.sptd.TimeOutValue = timeout_sec ? timeout_sec : 10;
    wb.sptd.DataBuffer = data;
    wb.sptd.SenseInfoOffset = (ULONG)((char*)wb.sense - (char*)&wb);
    memcpy(wb.sptd.Cdb, cdb, cdb_len);

    if(!DeviceIoControl(p->h, IOCTL_SCSI_PASS_THROUGH_DIRECT,
                        &wb, sizeof(wb), &wb, sizeof(wb), &returned, NULL))
    {
        set_last_error("SCSI pass-through failed on '%s' (Win32 error %lu)", p->device, (unsigned long)GetLastError());
        return false;
    }
    if(wb.sptd.ScsiStatus != 0)
    {
        set_last_error("SCSI command failed on '%s' (status 0x%02x, sense %02x/%02x/%02x)",
                       p->device, (unsigned)wb.sptd.ScsiStatus, wb.sense[2] & 0x0f, wb.sense[12], wb.sense[13]);
        return false;
    }
    return true;
}
#else
static bool native_open_named(CDPhys *p, const char *dev)
{
    p->fd = open(dev, O_RDONLY | O_NONBLOCK);
    if(p->fd < 0)
    {
        set_last_error("failed to open physical CD device '%s': %s", dev, strerror(errno));
        return false;
    }
    snprintf(p->device, sizeof(p->device), "%s", dev);
    return true;
}

static bool native_open_device(CDPhys *p, const char *path)
{
    static const char *const candidates[] = {
        "/dev/cdrom", "/dev/sr0", "/dev/cdrw", "/dev/dvd", "/dev/scd0", "/dev/sg0"
    };
    const char *payload = path_payload(path);
    if(payload[0])
        return native_open_named(p, payload);

    for(size_t i = 0; i < ARRAY_SIZE(candidates); i++)
    {
        if(native_open_named(p, candidates[i]))
            return true;
    }
    set_last_error("no Linux CD-ROM device could be opened; use cdrom:/dev/sr0 or another explicit device path");
    return false;
}

static void native_close_device(CDPhys *p)
{
    if(p && p->fd >= 0)
    {
        close(p->fd);
        p->fd = -1;
    }
}

static bool native_scsi_read(CDPhys *p, const uint8_t *cdb, uint8_t cdb_len, void *data, uint32_t data_len, unsigned timeout_sec)
{
    sg_io_hdr_t io;
    uint8_t sense[32];
    uint8_t cdb_copy[16];

    memset(&io, 0, sizeof(io));
    memset(sense, 0, sizeof(sense));
    memset(cdb_copy, 0, sizeof(cdb_copy));
    memcpy(cdb_copy, cdb, cdb_len);

    io.interface_id = 'S';
    io.dxfer_direction = SG_DXFER_FROM_DEV;
    io.cmd_len = cdb_len;
    io.mx_sb_len = sizeof(sense);
    io.dxfer_len = data_len;
    io.dxferp = data;
    io.cmdp = cdb_copy;
    io.sbp = sense;
    io.timeout = (timeout_sec ? timeout_sec : 10) * 1000;

    if(ioctl(p->fd, SG_IO, &io) < 0)
    {
        set_last_error("SG_IO failed on '%s': %s", p->device, strerror(errno));
        return false;
    }
    if((io.info & SG_INFO_OK_MASK) != SG_INFO_OK)
    {
        set_last_error("SCSI command failed on '%s' (status 0x%02x, host 0x%02x, driver 0x%02x, sense %02x/%02x/%02x)",
                       p->device, (unsigned)io.status, (unsigned)io.host_status, (unsigned)io.driver_status,
                       sense[2] & 0x0f, sense[12], sense[13]);
        return false;
    }
    return true;
}
#endif

static bool read_toc_full(CDPhys *p, TOC *toc)
{
    uint8_t cdb[10];
    uint8_t *buf;
    const size_t alloc = 0x3fff;
    int len_counter;
    uint8_t *tbi;

    buf = (uint8_t*)calloc(1, alloc);
    if(!buf)
    {
        set_last_error("out of memory reading physical CD TOC");
        return false;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x43;       /* READ TOC/PMA/ATIP */
    cdb[2] = 0x02;       /* Full TOC, same format Mednafen uses. */
    cdb[6] = 0x01;       /* First session. */
    cdb[7] = (uint8_t)(alloc >> 8);
    cdb[8] = (uint8_t)alloc;

    if(!native_scsi_read(p, cdb, sizeof(cdb), buf, (uint32_t)alloc, 10))
    {
        free(buf);
        return false;
    }

    TOC_Clear(toc);
    toc->disc_type = DISC_TYPE_CDDA_OR_M1;
    len_counter = (int)MDFN_de16msb(buf) - 2;
    tbi = buf + 4;
    if(len_counter < 0 || (len_counter % 11) != 0)
    {
        free(buf);
        set_last_error("physical CD READ TOC full response has invalid length");
        return false;
    }

    while(len_counter > 0)
    {
        uint8_t adr_ctrl = tbi[1];
        uint8_t point = tbi[3];
        uint8_t pmin = tbi[8];
        uint8_t psec = tbi[9];
        uint8_t pframe = tbi[10];

        if((adr_ctrl >> 4) == ADR_CURPOS)
        {
            if(point >= 1 && point <= 99)
            {
                toc->tracks[point].adr = adr_ctrl >> 4;
                toc->tracks[point].control = adr_ctrl & 0x0f;
                toc->tracks[point].lba = (uint32_t)AMSF_to_LBA(pmin, psec, pframe);
                toc->tracks[point].valid = true;
            }
            else if(point == 0xA0)
            {
                toc->first_track = pmin;
                toc->disc_type = psec;
            }
            else if(point == 0xA1)
                toc->last_track = pmin;
            else if(point == 0xA2)
            {
                toc->tracks[100].adr = adr_ctrl >> 4;
                toc->tracks[100].control = adr_ctrl & 0x0f;
                toc->tracks[100].lba = (uint32_t)AMSF_to_LBA(pmin, psec, pframe);
                toc->tracks[100].valid = true;
            }
        }
        tbi += 11;
        len_counter -= 11;
    }
    free(buf);

    if(toc->first_track < 1 || toc->first_track > 99 || toc->last_track < toc->first_track || toc->last_track > 99 || !toc->tracks[100].valid)
    {
        set_last_error("physical CD full TOC is missing required track or leadout entries");
        return false;
    }
    return true;
}

static bool read_toc_formatted(CDPhys *p, TOC *toc)
{
    uint8_t cdb[10];
    uint8_t buf[4096];
    int len;
    int off;

    memset(buf, 0, sizeof(buf));
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x43;       /* READ TOC/PMA/ATIP */
    cdb[2] = 0x00;       /* Formatted TOC. */
    cdb[7] = (uint8_t)(sizeof(buf) >> 8);
    cdb[8] = (uint8_t)sizeof(buf);

    if(!native_scsi_read(p, cdb, sizeof(cdb), buf, sizeof(buf), 10))
        return false;

    TOC_Clear(toc);
    toc->disc_type = DISC_TYPE_CDDA_OR_M1;
    len = (int)MDFN_de16msb(buf);
    if(len < 2 || len + 2 > (int)sizeof(buf))
    {
        set_last_error("physical CD formatted TOC response has invalid length");
        return false;
    }

    toc->first_track = buf[2];
    toc->last_track = buf[3];
    for(off = 4; off + 7 < len + 2; off += 8)
    {
        uint8_t adr_ctrl = buf[off + 1];
        uint8_t track = buf[off + 2];
        uint32_t lba = be32(buf + off + 4);
        int slot = (track == 0xAA) ? 100 : track;
        if(slot >= 1 && slot <= 100)
        {
            toc->tracks[slot].adr = adr_ctrl >> 4;
            toc->tracks[slot].control = adr_ctrl & 0x0f;
            toc->tracks[slot].lba = lba;
            toc->tracks[slot].valid = true;
        }
    }
    if(toc->first_track < 1 || toc->first_track > 99 || toc->last_track < toc->first_track || toc->last_track > 99 || !toc->tracks[100].valid)
    {
        set_last_error("physical CD formatted TOC is missing required track or leadout entries");
        return false;
    }
    return true;
}

static bool read_toc(CDPhys *p, TOC *toc)
{
    char full_error[sizeof(cdphys_last_error)];
    if(read_toc_full(p, toc))
        return true;
    snprintf(full_error, sizeof(full_error), "%s", CDPhys_LastError());
    if(read_toc_formatted(p, toc))
        return true;
    set_last_error("unable to read physical CD TOC; full TOC error: %s; formatted TOC error: %s", full_error, CDPhys_LastError());
    return false;
}

CDPhys *CDPhys_Open(const char *path, TOC *out_toc)
{
    CDPhys *p;
    cdphys_last_error[0] = 0;

    p = (CDPhys*)calloc(1, sizeof(*p));
    if(!p)
    {
        set_last_error("out of memory opening physical CD");
        return NULL;
    }
#ifdef _WIN32
    p->h = INVALID_HANDLE_VALUE;
#else
    p->fd = -1;
#endif

    if(!native_open_device(p, path))
    {
        free(p);
        return NULL;
    }
    if(!read_toc(p, &p->toc))
    {
        native_close_device(p);
        free(p);
        return NULL;
    }
    if(out_toc)
        *out_toc = p->toc;
    return p;
}

void CDPhys_Close(CDPhys *p)
{
    if(!p)
        return;
    native_close_device(p);
    free(p);
}

bool CDPhys_ReadRawSector(CDPhys *p, uint8_t *buf, int32_t lba)
{
    uint8_t cdb[12];
    if(!p || !buf)
        return false;
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0xBE;       /* READ CD */
    cdb[1] = 0x00;       /* Expected sector type: any. */
    put_be32(cdb + 2, (uint32_t)lba);
    put_be24(cdb + 6, 1);
    cdb[9] = 0xF8;       /* Sync + all headers + user data + EDC/ECC. */
    cdb[10] = 0x01;      /* Raw P-W subchannel, Mednafen-compatible. */
    memset(buf, 0, 2352 + 96);
    return native_scsi_read(p, cdb, sizeof(cdb), buf, 2352 + 96, 10);
}

#else
/* Physical CD-ROM support is intentionally compiled out unless
 * PCFX_ENABLE_PHYSICAL_CD is defined by a supported frontend build. */
int pcfx_cdrom_phys_disabled_translation_unit;
#endif
