#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdbool.h>

#include "../mednafen.h"
#include "../mednafen-endian.h"
#include "CDUtility.h"
#include "cdromif.h"
#ifdef PCFX_ENABLE_PHYSICAL_CD
#include "cdrom_phys.h"
#endif
#ifdef HAVE_CHD
#include <libchdr/chd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CDIF_LBA_READ_MINIMUM (-150)
#define CDIF_LBA_READ_MAXIMUM 449849

typedef enum
{
    TRACK_AUDIO = 0,
    TRACK_MODE1_2352,
    TRACK_MODE1_2048,
    TRACK_MODE2_2352,
    TRACK_UNKNOWN
} TrackFormat;

typedef enum
{
    DI_FORMAT_AUDIO = 0x00,
    DI_FORMAT_MODE1 = 0x01,
    DI_FORMAT_MODE1_RAW = 0x02,
    DI_FORMAT_MODE2 = 0x03,
    DI_FORMAT_MODE2_FORM1 = 0x04,
    DI_FORMAT_MODE2_FORM2 = 0x05,
    DI_FORMAT_MODE2_RAW = 0x06,
    DI_FORMAT_CDI_RAW = 0x07
} DIFormat;

typedef struct CDTrack
{
    int number;
    uint8_t subq_control;
    int32_t lba;
    int32_t sectors;
    int32_t pregap;
    int32_t pregap_dv;
    int32_t postgap;
    int32_t index[100];      /* absolute LBA after parse, INT32_MAX if absent */
    int32_t file_index1;     /* sector offset in file for INDEX 01 */
    int32_t chd_file_offset; /* sector offset in CHD logical stream */
    int sector_size;
    TrackFormat format;
    DIFormat di_format;
    bool from_chd;
    bool raw_audio_msb_first;
    bool file_is_wave;
    int64_t file_data_offset;
    int64_t file_data_bytes;
    char path[PATH_MAX];
} CDTrack;

struct CDIF
{
    bool unrecoverable_error;
    TOC disc_toc;
    int num_tracks;
    int first_track;
    int last_track;
    int32_t total_sectors;
    bool is_chd;
#ifdef PCFX_ENABLE_PHYSICAL_CD
    bool is_physical;
    CDPhys *physical;
#endif
#ifdef HAVE_CHD
    chd_file *chd;
    uint8_t *chd_hunkmem;
    int chd_oldhunk;
#endif
    CDTrack tracks[101];
};

static char *trim_left(char *s)
{
    while(*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void trim_right(char *s)
{
    size_t n = strlen(s);
    while(n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static int ends_with_ci(const char *s, const char *suffix)
{
    size_t sl = strlen(s), xl = strlen(suffix);
    if(sl < xl) return 0;
    return strcasecmp(s + sl - xl, suffix) == 0;
}

static void path_dirname(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if(!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if(!slash)
        snprintf(out, out_size, ".");
    else
    {
        size_t n = (size_t)(slash - path);
        if(n >= out_size) n = out_size - 1;
        memcpy(out, path, n);
        out[n] = 0;
    }
}

static void path_join(char *out, size_t out_size, const char *dir, const char *leaf)
{
    if(!leaf || !leaf[0]) { snprintf(out, out_size, "%s", dir ? dir : "."); return; }
    if(leaf[0] == '/' || (strlen(leaf) > 2 && leaf[1] == ':'))
        snprintf(out, out_size, "%s", leaf);
    else if(!dir || !dir[0] || !strcmp(dir, "."))
        snprintf(out, out_size, "%s", leaf);
    else
        snprintf(out, out_size, "%s/%s", dir, leaf);
}

static int parse_msf(const char *s)
{
    int m = 0, sec = 0, f = 0;
    if(sscanf(s, "%d:%d:%d", &m, &sec, &f) != 3)
        return 0;
    return (m * 60 + sec) * 75 + f;
}

static int64_t file_size_bytes(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if(!fp) return -1;
    if(fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long pos = ftell(fp);
    fclose(fp);
    return pos < 0 ? -1 : (int64_t)pos;
}



static uint16_t read_le16_buf(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32_buf(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool parse_wave_pcm_data(const char *path, int64_t *data_offset, int64_t *data_bytes)
{
    FILE *fp;
    uint8_t hdr[12];
    bool have_fmt = false;
    bool have_data = false;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;

    if(data_offset) *data_offset = 0;
    if(data_bytes) *data_bytes = 0;
    if(!path || !data_offset || !data_bytes)
        return false;

    fp = fopen(path, "rb");
    if(!fp)
        return false;
    if(fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr) || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4))
    {
        fclose(fp);
        return false;
    }

    while(!have_data)
    {
        uint8_t ch[8];
        uint32_t chunk_size;
        long chunk_data_pos;
        if(fread(ch, 1, sizeof(ch), fp) != sizeof(ch))
            break;
        chunk_size = read_le32_buf(ch + 4);
        chunk_data_pos = ftell(fp);
        if(chunk_data_pos < 0)
            break;

        if(!memcmp(ch, "fmt ", 4))
        {
            uint8_t fmt[40];
            size_t want = chunk_size < sizeof(fmt) ? (size_t)chunk_size : sizeof(fmt);
            memset(fmt, 0, sizeof(fmt));
            if(want < 16 || fread(fmt, 1, want, fp) != want)
                break;
            audio_format = read_le16_buf(fmt + 0);
            channels = read_le16_buf(fmt + 2);
            sample_rate = read_le32_buf(fmt + 4);
            bits_per_sample = read_le16_buf(fmt + 14);
            have_fmt = true;
        }
        else if(!memcmp(ch, "data", 4))
        {
            *data_offset = (int64_t)chunk_data_pos;
            *data_bytes = (int64_t)chunk_size;
            have_data = true;
        }

        if(fseek(fp, chunk_data_pos + (long)chunk_size + (long)(chunk_size & 1U), SEEK_SET) != 0)
            break;
    }

    fclose(fp);

    /* CUE FILE WAVE tracks are CD-DA sectors stored as normal PCM WAV data.
       Feeding the RIFF/fmt header to the CDDA mixer causes a loud click/glitch
       and shifts every following sample by the header size.  Only accept the
       CD-DA-compatible format here; unsupported WAV variants should fail load
       instead of being interpreted as raw PCM sectors. */
    if(!have_fmt || !have_data || audio_format != 1 || channels != 2 || sample_rate != 44100 || bits_per_sample != 16)
        return false;
    if(*data_bytes < 2352)
        return false;
    return true;
}
static int quoted_arg(const char *line, char *out, size_t out_size)
{
    const char *p = strchr(line, '"');
    if(!p) return 0;
    p++;
    const char *q = strchr(p, '"');
    if(!q) return 0;
    size_t n = (size_t)(q - p);
    if(n >= out_size) n = out_size - 1;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

static int file_arg_and_type(const char *line, char *out, size_t out_size, char *type, size_t type_size)
{
    const char *p;
    const char *q;
    size_t n;

    if(out && out_size) out[0] = 0;
    if(type && type_size) type[0] = 0;
    if(!line || !out || out_size == 0)
        return 0;

    p = line + 4;
    while(*p && isspace((unsigned char)*p)) p++;
    if(*p == '"')
    {
        p++;
        q = strchr(p, '"');
        if(!q)
            return 0;
        n = (size_t)(q - p);
        if(n >= out_size) n = out_size - 1;
        memcpy(out, p, n);
        out[n] = 0;
        p = q + 1;
    }
    else
    {
        q = p;
        while(*q && !isspace((unsigned char)*q)) q++;
        if(q == p)
            return 0;
        n = (size_t)(q - p);
        if(n >= out_size) n = out_size - 1;
        memcpy(out, p, n);
        out[n] = 0;
        p = q;
    }

    while(*p && isspace((unsigned char)*p)) p++;
    if(type && type_size && *p)
    {
        q = p;
        while(*q && !isspace((unsigned char)*q)) q++;
        n = (size_t)(q - p);
        if(n >= type_size) n = type_size - 1;
        memcpy(type, p, n);
        type[n] = 0;
    }
    return 1;
}

static TrackFormat parse_track_format(const char *line, int *sector_size, uint8_t *control)
{
    *sector_size = 2352;
    *control = 0;
    if(strstr(line, "AUDIO"))
    {
        *sector_size = 2352;
        *control = 0;
        return TRACK_AUDIO;
    }
    if(strstr(line, "MODE1/2352"))
    {
        *sector_size = 2352;
        *control = SUBQ_CTRLF_DATA;
        return TRACK_MODE1_2352;
    }
    if(strstr(line, "MODE1/2048"))
    {
        *sector_size = 2048;
        *control = SUBQ_CTRLF_DATA;
        return TRACK_MODE1_2048;
    }
    if(strstr(line, "MODE2/2352"))
    {
        *sector_size = 2352;
        *control = SUBQ_CTRLF_DATA;
        return TRACK_MODE2_2352;
    }
    return TRACK_UNKNOWN;
}

static void generate_toc(CDIF *cdif)
{
    TOC_Clear(&cdif->disc_toc);
    cdif->disc_toc.first_track = (uint8_t)cdif->first_track;
    cdif->disc_toc.last_track = (uint8_t)cdif->last_track;
    cdif->disc_toc.disc_type = DISC_TYPE_CDDA_OR_M1;
    for(int i = cdif->first_track; i <= cdif->last_track; i++)
    {
        CDTrack *t = &cdif->tracks[i];
        cdif->disc_toc.tracks[i].lba = (uint32_t)t->lba;
        cdif->disc_toc.tracks[i].adr = ADR_CURPOS;
        cdif->disc_toc.tracks[i].control = t->subq_control;
        cdif->disc_toc.tracks[i].valid = true;
    }
    cdif->disc_toc.tracks[100].lba = (uint32_t)cdif->total_sectors;
    cdif->disc_toc.tracks[100].adr = ADR_CURPOS;
    cdif->disc_toc.tracks[100].control = cdif->last_track > 0 ? cdif->tracks[cdif->last_track].subq_control : 0;
    cdif->disc_toc.tracks[100].valid = true;
}

static bool parse_cue(CDIF *cdif, const char *path)
{
    FILE *cue = fopen(path, "rb");
    if(!cue) return false;

    char base_dir[PATH_MAX];
    path_dirname(path, base_dir, sizeof(base_dir));

    char cur_file[PATH_MAX] = {0};
    bool cur_file_is_wave = false;
    char linebuf[4096];
    int current_track = 0;

    memset(cdif, 0, sizeof(*cdif));
    cdif->first_track = 1;
    for(int i = 0; i < 101; i++)
        for(int j = 0; j < 100; j++)
            cdif->tracks[i].index[j] = -1;

    while(fgets(linebuf, sizeof(linebuf), cue))
    {
        trim_right(linebuf);
        char *line = trim_left(linebuf);
        if(!strncasecmp(line, "FILE", 4))
        {
            char leaf[PATH_MAX];
            char file_type[32];
            if(file_arg_and_type(line, leaf, sizeof(leaf), file_type, sizeof(file_type)))
            {
                path_join(cur_file, sizeof(cur_file), base_dir, leaf);
                cur_file_is_wave = ends_with_ci(leaf, ".wav") || !strcasecmp(file_type, "WAVE");
            }
        }
        else if(!strncasecmp(line, "TRACK", 5))
        {
            int tn = 0;
            if(sscanf(line, "TRACK %d", &tn) == 1 && tn > 0 && tn <= 99)
            {
                current_track = tn;
                CDTrack *t = &cdif->tracks[tn];
                memset(t, 0, sizeof(*t));
                for(int j = 0; j < 100; j++) t->index[j] = -1;
                t->number = tn;
                t->format = parse_track_format(line, &t->sector_size, &t->subq_control);
                if(t->format == TRACK_AUDIO) t->di_format = DI_FORMAT_AUDIO;
                else if(t->format == TRACK_MODE1_2048) t->di_format = DI_FORMAT_MODE1;
                else if(t->format == TRACK_MODE1_2352) t->di_format = DI_FORMAT_MODE1_RAW;
                else if(t->format == TRACK_MODE2_2352) t->di_format = DI_FORMAT_MODE2_RAW;
                snprintf(t->path, sizeof(t->path), "%s", cur_file);
                t->file_is_wave = cur_file_is_wave && t->format == TRACK_AUDIO;
                t->file_data_offset = 0;
                t->file_data_bytes = 0;
                if(tn > cdif->last_track) cdif->last_track = tn;
                if(cdif->num_tracks == 0 || tn < cdif->first_track) cdif->first_track = tn;
                cdif->num_tracks++;
            }
        }
        else if(!strncasecmp(line, "INDEX", 5) && current_track > 0)
        {
            char *p = line + 5;
            while(*p && isspace((unsigned char)*p)) p++;
            char *endp = p;
            long idx_l = strtol(p, &endp, 10);
            if(endp != p && idx_l >= 0 && idx_l < 100)
            {
                p = endp;
                while(*p && isspace((unsigned char)*p)) p++;
                char msf[64] = {0};
                size_t n = 0;
                while(p[n] && !isspace((unsigned char)p[n]) && n + 1 < sizeof(msf))
                {
                    msf[n] = p[n];
                    n++;
                }
                msf[n] = 0;
                if(n > 0)
                    cdif->tracks[current_track].index[(int)idx_l] = parse_msf(msf);
            }
        }
    }
    fclose(cue);

    if(cdif->num_tracks <= 0) return false;

    int32_t running_lba = 0;
    for(int tn = cdif->first_track; tn <= cdif->last_track; tn++)
    {
        CDTrack *t = &cdif->tracks[tn];
        if(t->number == 0) continue;
        if(t->index[1] < 0) t->index[1] = 0;

        /*
         * Multi-BIN CUE sheets may store the pregap in the same file as the
         * following track, represented as INDEX 00 .. INDEX 01.  N-nyuu is an
         * important PC-FXGA example: the bootable data track is track 2 and its
         * first 225 sectors live before INDEX 01.  The old file-backed CUE path
         * treated that range as synthesized silence/empty sectors, while the
         * CHD path correctly exposed the pregap payload.  Split the CUE pregap
         * into an on-disc/file-backed portion (pregap_dv) and a synthesized
         * portion (pregap) so BIN/CUE and CHD layouts are read consistently.
         */
        t->file_index1 = t->index[1];
        if(t->index[0] >= 0 && t->index[0] <= t->index[1])
        {
            t->pregap_dv = t->index[1] - t->index[0];
            t->pregap = t->index[0];
        }
        else
        {
            t->pregap_dv = 0;
            t->pregap = t->index[1];
        }
        t->lba = running_lba + t->pregap + t->pregap_dv;

        int64_t fsize = file_size_bytes(t->path);
        if(fsize < 0 || t->sector_size <= 0) return false;
        if(t->file_is_wave)
        {
            if(!parse_wave_pcm_data(t->path, &t->file_data_offset, &t->file_data_bytes))
                return false;
            fsize = t->file_data_bytes;
        }
        int32_t file_sectors = (int32_t)(fsize / t->sector_size);
        t->sectors = file_sectors - t->file_index1;
        if(t->sectors < 0) t->sectors = 0;

        int32_t base = t->index[1];
        for(int i = 0; i < 100; i++)
        {
            if(i == 0 || t->index[i] < 0) t->index[i] = INT32_MAX;
            else t->index[i] = t->lba + (t->index[i] - base);
        }
        running_lba = t->lba + t->sectors;
    }
    cdif->total_sectors = running_lba;
    generate_toc(cdif);
    return true;
}

static bool parse_single_raw(CDIF *cdif, const char *path)
{
    int64_t fsize = file_size_bytes(path);
    if(fsize <= 0) return false;
    memset(cdif, 0, sizeof(*cdif));
    cdif->num_tracks = 1;
    cdif->first_track = cdif->last_track = 1;
    CDTrack *t = &cdif->tracks[1];
    memset(t, 0, sizeof(*t));
    for(int i = 0; i < 100; i++) t->index[i] = INT32_MAX;
    t->index[1] = 0;
    t->number = 1;
    t->format = TRACK_MODE1_2352;
    t->di_format = DI_FORMAT_MODE1_RAW;
    t->subq_control = SUBQ_CTRLF_DATA;
    t->sector_size = 2352;
    t->lba = 0;
    t->sectors = (int32_t)(fsize / 2352);
    snprintf(t->path, sizeof(t->path), "%s", path);
    cdif->total_sectors = t->sectors;
    generate_toc(cdif);
    return true;
}


#ifdef HAVE_CHD
static bool parse_chd(CDIF *cdif, const char *path)
{
    if(!cdif || !path) return false;

    chd_error err = chd_open(path, CHD_OPEN_READ, NULL, &cdif->chd);
    if(err != CHDERR_NONE)
        return false;

    const chd_header *head = chd_get_header(cdif->chd);
    if(!head || head->hunkbytes < (2352 + 96))
        return false;

    cdif->chd_hunkmem = (uint8_t*)malloc(head->hunkbytes);
    if(!cdif->chd_hunkmem)
        return false;
    cdif->chd_oldhunk = -1;
    cdif->is_chd = true;

    TOC_Clear(&cdif->disc_toc);
    cdif->disc_toc.disc_type = DISC_TYPE_CDDA_OR_M1;
    cdif->first_track = 1;
    cdif->last_track = 0;
    cdif->num_tracks = 0;

    int32_t plba = -150;
    int32_t numsectors = 0;
    int32_t file_offset = 0;

    for(;;)
    {
        int tkid = 0, frames = 0, pregap = 0, postgap = 0;
        char type[64] = {0}, subtype[32] = {0}, pgtype[32] = {0}, pgsub[32] = {0};
        char meta[512] = {0};

        err = chd_get_metadata(cdif->chd, CDROM_TRACK_METADATA2_TAG, (uint32_t)cdif->num_tracks,
                               meta, sizeof(meta), NULL, NULL, NULL);
        if(err == CHDERR_NONE)
        {
            if(sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &tkid, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap) < 8)
                return false;
        }
        else
        {
            err = chd_get_metadata(cdif->chd, CDROM_TRACK_METADATA_TAG, (uint32_t)cdif->num_tracks,
                                   meta, sizeof(meta), NULL, NULL, NULL);
            if(err != CHDERR_NONE)
                break;
            if(sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &tkid, type, subtype, &frames) < 4)
                return false;
            pregap = 0;
            postgap = 0;
            strcpy(pgtype, "NONE");
            strcpy(pgsub, "NONE");
        }

        if(frames <= 0 || cdif->num_tracks >= 99)
            return false;
        if(strcmp(type, "MODE1") && strcmp(type, "MODE1_RAW") && strcmp(type, "MODE2_RAW") && strcmp(type, "AUDIO"))
            return false;
        if(strcmp(subtype, "NONE"))
            return false;

        cdif->num_tracks++;
        const int tn = cdif->num_tracks;
        CDTrack *t = &cdif->tracks[tn];
        memset(t, 0, sizeof(*t));
        for(int i = 0; i < 100; i++) t->index[i] = -1;

        t->number = tn;
        t->from_chd = true;
        t->pregap = (tn == 1) ? 150 : (pgtype[0] == 'V' ? 0 : pregap);
        t->pregap_dv = (pgtype[0] == 'V') ? pregap : 0;
        t->postgap = postgap;
        t->index[0] = -1;
        t->index[1] = 0;
        t->sector_size = 2352;

        plba += t->pregap + t->pregap_dv;
        t->lba = plba;
        t->sectors = frames - t->pregap_dv;
        if(t->sectors < 0) t->sectors = 0;

        file_offset += t->pregap_dv;
        t->chd_file_offset = file_offset;
        file_offset += frames - t->pregap_dv;
        file_offset += t->postgap;
        file_offset += ((frames + 3) & ~3) - frames;

        if(!strcmp(type, "AUDIO"))
        {
            t->format = TRACK_AUDIO;
            t->di_format = DI_FORMAT_AUDIO;
            t->subq_control = 0;
            t->raw_audio_msb_first = true;
        }
        else if(!strcmp(type, "MODE1"))
        {
            t->format = TRACK_MODE1_2048;
            t->di_format = DI_FORMAT_MODE1;
            t->subq_control = SUBQ_CTRLF_DATA;
        }
        else if(!strcmp(type, "MODE1_RAW"))
        {
            t->format = TRACK_MODE1_2352;
            t->di_format = DI_FORMAT_MODE1_RAW;
            t->subq_control = SUBQ_CTRLF_DATA;
        }
        else if(!strcmp(type, "MODE2_RAW"))
        {
            t->format = TRACK_MODE2_2352;
            t->di_format = DI_FORMAT_MODE2_RAW;
            t->subq_control = SUBQ_CTRLF_DATA;
        }

        cdif->disc_toc.tracks[tn].adr = ADR_CURPOS;
        cdif->disc_toc.tracks[tn].control = t->subq_control;
        cdif->disc_toc.tracks[tn].lba = (uint32_t)t->lba;
        cdif->disc_toc.tracks[tn].valid = true;
        cdif->last_track = tn;

        plba += frames - t->pregap_dv;
        plba += t->postgap;
        numsectors += (tn == 1) ? frames : frames + t->pregap;
    }

    if(cdif->num_tracks <= 0)
        return false;

    cdif->first_track = 1;
    cdif->total_sectors = numsectors;
    cdif->disc_toc.first_track = (uint8_t)cdif->first_track;
    cdif->disc_toc.last_track = (uint8_t)cdif->last_track;
    cdif->disc_toc.tracks[100].adr = ADR_CURPOS;
    cdif->disc_toc.tracks[100].control = 0;
    cdif->disc_toc.tracks[100].lba = (uint32_t)cdif->total_sectors;
    cdif->disc_toc.tracks[100].valid = true;

    for(int tn = cdif->first_track; tn <= cdif->last_track; tn++)
    {
        CDTrack *t = &cdif->tracks[tn];
        const int32_t base = t->index[1];
        for(int i = 0; i < 100; i++)
        {
            if(i == 0 || t->index[i] < 0)
                t->index[i] = INT32_MAX;
            else
                t->index[i] = t->lba + (t->index[i] - base);
        }
    }

    return true;
}

static bool chd_read_hunk(CDIF *cdif, uint8_t *dst, int32_t lba, const CDTrack *t, size_t dst_offset, size_t bytes)
{
    if(!cdif || !cdif->chd || !cdif->chd_hunkmem || !t || !dst)
        return false;
    const chd_header *head = chd_get_header(cdif->chd);
    const int bytes_per_sector = 2352 + 96;
    const int sectors_per_hunk = (int)(head->hunkbytes / bytes_per_sector);
    if(sectors_per_hunk <= 0)
        return false;
    const int32_t cad = lba - t->lba + t->chd_file_offset;
    if(cad < 0)
        return false;
    const int hunknum = cad / sectors_per_hunk;
    const int hunkofs = cad % sectors_per_hunk;
    if(hunknum != cdif->chd_oldhunk)
    {
        chd_error err = chd_read(cdif->chd, (uint32_t)hunknum, cdif->chd_hunkmem);
        if(err != CHDERR_NONE)
            return false;
        cdif->chd_oldhunk = hunknum;
    }
    memcpy(dst + dst_offset, cdif->chd_hunkmem + (size_t)hunkofs * (size_t)bytes_per_sector, bytes);
    return true;
}
#endif

static int find_track_for_lba(CDIF *cdif, int32_t lba)
{
    int best = cdif->first_track;
    for(int tn = cdif->first_track; tn <= cdif->last_track; tn++)
    {
        CDTrack *t = &cdif->tracks[tn];
        if(!t->number)
            continue;
        const int32_t start = t->lba - t->pregap_dv - t->pregap;
        const int32_t end = t->lba + t->sectors + t->postgap;
        if(lba >= start)
            best = tn;
        if(t->from_chd && lba >= start && lba < end)
            return tn;
    }
    return best;
}

static int make_subpq(CDIF *cdif, int32_t lba, uint8_t *SubPWBuf)
{
    uint8_t buf[12];
    int track = find_track_for_lba(cdif, lba);
    CDTrack *t = &cdif->tracks[track];
    uint32_t lba_relative;
    uint8_t pause_or = 0;

    if(lba < t->lba) lba_relative = (uint32_t)(t->lba - 1 - lba);
    else lba_relative = (uint32_t)(lba - t->lba);

    uint32_t f = lba_relative % 75;
    uint32_t s = (lba_relative / 75) % 60;
    uint32_t m = (lba_relative / 75 / 60);
    uint32_t fa = (lba + 150) % 75;
    uint32_t sa = ((lba + 150) / 75) % 60;
    uint32_t ma = ((lba + 150) / 75 / 60);
    uint8_t control = t->subq_control;

    if(lba < t->lba || lba >= t->lba + t->sectors)
        pause_or = 0x80;

    int32_t pg_offset = lba - t->lba;
    if(pg_offset < -150 && (t->subq_control & SUBQ_CTRLF_DATA) && track > cdif->first_track && !(cdif->tracks[track - 1].subq_control & SUBQ_CTRLF_DATA))
        control = cdif->tracks[track - 1].subq_control;

    memset(buf, 0, sizeof(buf));
    buf[0] = (ADR_CURPOS << 0) | (control << 4);
    buf[1] = U8_to_BCD((uint8_t)track);
    int index = 0;
    for(int i = 0; i < 100; i++)
        if(lba >= t->index[i]) index = i;
    buf[2] = U8_to_BCD((uint8_t)index);
    buf[3] = U8_to_BCD((uint8_t)m);
    buf[4] = U8_to_BCD((uint8_t)s);
    buf[5] = U8_to_BCD((uint8_t)f);
    buf[6] = 0;
    buf[7] = U8_to_BCD((uint8_t)ma);
    buf[8] = U8_to_BCD((uint8_t)sa);
    buf[9] = U8_to_BCD((uint8_t)fa);
    subq_generate_checksum(buf);

    for(int i = 0; i < 96; i++)
        SubPWBuf[i] |= (((buf[i >> 3] >> (7 - (i & 7))) & 1) ? 0x40 : 0x00) | pause_or;
    return track;
}


#ifdef PCFX_ENABLE_PHYSICAL_CD
bool CDIF_IsPhysicalPath_C(const char *path)
{
    return CDPhys_IsPath(path);
}

static bool parse_physical(CDIF *cdif, const char *path)
{
    if(!cdif || !path)
        return false;
    memset(cdif, 0, sizeof(*cdif));
    cdif->physical = CDPhys_Open(path, &cdif->disc_toc);
    if(!cdif->physical)
        return false;
    cdif->is_physical = true;
    cdif->first_track = cdif->disc_toc.first_track;
    cdif->last_track = cdif->disc_toc.last_track;
    cdif->num_tracks = (cdif->last_track >= cdif->first_track) ? (cdif->last_track - cdif->first_track + 1) : 0;
    cdif->total_sectors = (int32_t)cdif->disc_toc.tracks[100].lba;
    for(int i = cdif->first_track; i <= cdif->last_track && i <= 99; i++)
    {
        CDTrack *t = &cdif->tracks[i];
        memset(t, 0, sizeof(*t));
        for(int j = 0; j < 100; j++)
            t->index[j] = INT32_MAX;
        t->index[1] = (int32_t)cdif->disc_toc.tracks[i].lba;
        t->number = i;
        t->subq_control = cdif->disc_toc.tracks[i].control;
        t->sector_size = 2352;
        t->lba = (int32_t)cdif->disc_toc.tracks[i].lba;
        int next = (i == cdif->last_track) ? 100 : (i + 1);
        t->sectors = (int32_t)cdif->disc_toc.tracks[next].lba - t->lba;
        if(t->sectors < 0)
            t->sectors = 0;
        t->format = (t->subq_control & SUBQ_CTRLF_DATA) ? TRACK_MODE1_2352 : TRACK_AUDIO;
        t->di_format = (t->subq_control & SUBQ_CTRLF_DATA) ? DI_FORMAT_MODE1_RAW : DI_FORMAT_AUDIO;
    }
    return cdif->num_tracks > 0 && cdif->total_sectors > 0;
}
#else
bool CDIF_IsPhysicalPath_C(const char *path)
{
    (void)path;
    return false;
}
#endif

CDIF *CDIF_Open_C(const char *path, bool image_memcache)
{
    (void)image_memcache;
    if(!path) return NULL;
    CDIF *cdif = (CDIF*)calloc(1, sizeof(CDIF));
    if(!cdif) return NULL;
    bool ok = false;
#ifdef PCFX_ENABLE_PHYSICAL_CD
    if(CDPhys_IsPath(path))
        ok = parse_physical(cdif, path);
    else
#endif
    if(ends_with_ci(path, ".cue") || ends_with_ci(path, ".toc"))
        ok = parse_cue(cdif, path);
#ifdef HAVE_CHD
    else if(ends_with_ci(path, ".chd"))
        ok = parse_chd(cdif, path);
#endif
    else if(ends_with_ci(path, ".bin") || ends_with_ci(path, ".iso"))
        ok = parse_single_raw(cdif, path);
    if(!ok)
    {
#ifdef HAVE_CHD
        if(cdif->chd) chd_close(cdif->chd);
        free(cdif->chd_hunkmem);
#endif
#ifdef PCFX_ENABLE_PHYSICAL_CD
        if(cdif->physical) CDPhys_Close(cdif->physical);
#endif
        free(cdif);
        return NULL;
    }
    return cdif;
}

void CDIF_Close_C(CDIF *cdif)
{
    if(!cdif) return;
#ifdef HAVE_CHD
    if(cdif->chd) chd_close(cdif->chd);
    free(cdif->chd_hunkmem);
#endif
#ifdef PCFX_ENABLE_PHYSICAL_CD
    if(cdif->physical) CDPhys_Close(cdif->physical);
#endif
    free(cdif);
}

void CDIF_ReadTOC_C(CDIF *cdif, TOC *read_target)
{
    if(!cdif || !read_target) return;
    *read_target = cdif->disc_toc;
}

void CDIF_HintReadSector_C(CDIF *cdif, int32_t lba)
{
    (void)cdif;
    (void)lba;
}


#ifdef HAVE_CHD
static bool read_chd_raw_sector(CDIF *cdif, uint8_t *buf, int32_t lba)
{
    if(!cdif || !buf)
        return false;

    if(lba < CDIF_LBA_READ_MINIMUM || lba > CDIF_LBA_READ_MAXIMUM)
    {
        memset(buf, 0, 2352 + 96);
        return false;
    }
    if(lba < 0)
    {
        synth_udapp_sector_lba(0xff, &cdif->disc_toc, lba, 0, buf);
        return true;
    }
    if(lba >= cdif->total_sectors)
    {
        uint8_t mode = 0x01;
        CDTrack *last = &cdif->tracks[cdif->last_track];
        switch(last->di_format)
        {
            case DI_FORMAT_AUDIO:
                mode = 0xff;
                break;
            case DI_FORMAT_MODE2_RAW:
            case DI_FORMAT_MODE2_FORM1:
            case DI_FORMAT_MODE2_FORM2:
            case DI_FORMAT_MODE2:
            case DI_FORMAT_CDI_RAW:
                mode = 0x02;
                break;
            case DI_FORMAT_MODE1_RAW:
            case DI_FORMAT_MODE1:
            default:
                mode = 0x01;
                break;
        }
        synth_leadout_sector_lba(mode, &cdif->disc_toc, lba, buf);
        return true;
    }

    memset(buf, 0, 2352 + 96);
    const int track_num = make_subpq(cdif, lba, buf + 2352);
    CDTrack *t = &cdif->tracks[track_num];

    if(lba < (t->lba - t->pregap_dv) || lba >= (t->lba + t->sectors))
    {
        CDTrack *et = t;
        int32_t pg_offset = lba - t->lba;
        if(pg_offset < -150 && (t->subq_control & SUBQ_CTRLF_DATA) && track_num > cdif->first_track && !(cdif->tracks[track_num - 1].subq_control & SUBQ_CTRLF_DATA))
            et = &cdif->tracks[track_num - 1];
        memset(buf, 0, 2352);
        switch(et->di_format)
        {
            case DI_FORMAT_AUDIO:
                break;
            case DI_FORMAT_MODE2_RAW:
            case DI_FORMAT_MODE2_FORM1:
            case DI_FORMAT_MODE2_FORM2:
            case DI_FORMAT_MODE2:
            case DI_FORMAT_CDI_RAW:
                buf[12 + 6] = 0x20;
                buf[12 + 10] = 0x20;
                encode_mode2_form2_sector((uint32_t)(lba + 150), buf);
                break;
            case DI_FORMAT_MODE1_RAW:
            case DI_FORMAT_MODE1:
            default:
                encode_mode1_sector((uint32_t)(lba + 150), buf);
                break;
        }
        return true;
    }

    switch(t->di_format)
    {
        case DI_FORMAT_AUDIO:
            if(!chd_read_hunk(cdif, buf, lba, t, 0, 2352)) return false;
            if(t->raw_audio_msb_first) Endian_A16_Swap(buf, 588 * 2);
            return true;
        case DI_FORMAT_MODE1:
            if(!chd_read_hunk(cdif, buf, lba, t, 16, 2048)) return false;
            encode_mode1_sector((uint32_t)(lba + 150), buf);
            return true;
        case DI_FORMAT_MODE1_RAW:
        case DI_FORMAT_MODE2_RAW:
        case DI_FORMAT_CDI_RAW:
            return chd_read_hunk(cdif, buf, lba, t, 0, 2352);
        case DI_FORMAT_MODE2:
            if(!chd_read_hunk(cdif, buf, lba, t, 16, 2336)) return false;
            encode_mode2_sector((uint32_t)(lba + 150), buf);
            return true;
        case DI_FORMAT_MODE2_FORM1:
        case DI_FORMAT_MODE2_FORM2:
        default:
            return false;
    }
}
#endif

bool CDIF_ReadRawSector_C(CDIF *cdif, uint8_t *buf, int32_t lba)
{
    if(!cdif || !buf)
        return false;
#ifdef PCFX_ENABLE_PHYSICAL_CD
    if(cdif->is_physical)
    {
        if(lba < CDIF_LBA_READ_MINIMUM || lba > CDIF_LBA_READ_MAXIMUM)
        {
            memset(buf, 0, 2352 + 96);
            return false;
        }
        if(lba < 0)
        {
            synth_udapp_sector_lba(0xff, &cdif->disc_toc, lba, 0, buf);
            return true;
        }
        if(lba >= cdif->total_sectors)
        {
            synth_leadout_sector_lba(0xff, &cdif->disc_toc, lba, buf);
            return true;
        }
        return CDPhys_ReadRawSector(cdif->physical, buf, lba);
    }
#endif
#ifdef HAVE_CHD
    if(cdif->is_chd)
        return read_chd_raw_sector(cdif, buf, lba);
#endif
    if(lba < CDIF_LBA_READ_MINIMUM || lba > CDIF_LBA_READ_MAXIMUM)
    {
        memset(buf, 0, 2352 + 96);
        return false;
    }
    if(lba < 0)
    {
        synth_udapp_sector_lba(0xff, &cdif->disc_toc, lba, 0, buf);
        return true;
    }
    if(lba >= cdif->total_sectors)
    {
        synth_leadout_sector_lba(0xff, &cdif->disc_toc, lba, buf);
        return true;
    }

    memset(buf, 0, 2352 + 96);
    int track_num = make_subpq(cdif, lba, buf + 2352);
    CDTrack *t = &cdif->tracks[track_num];

    const int32_t file_start_lba = t->lba - t->pregap_dv;
    if(lba < file_start_lba || lba >= t->lba + t->sectors)
    {
        if(t->subq_control & SUBQ_CTRLF_DATA)
            encode_mode1_sector((uint32_t)(lba + 150), buf);
        return true;
    }

    FILE *fp = fopen(t->path, "rb");
    if(!fp)
        return false;
    int32_t rel = lba - t->lba;
    int64_t sector_index = (int64_t)t->file_index1 + rel;
    if(sector_index < 0)
    {
        fclose(fp);
        return false;
    }
    int64_t offset = t->file_data_offset + sector_index * (int64_t)t->sector_size;
    if(fseek(fp, (long)offset, SEEK_SET) != 0)
    {
        fclose(fp);
        return false;
    }
    bool ok = true;
    if(t->format == TRACK_AUDIO || t->format == TRACK_MODE1_2352 || t->format == TRACK_MODE2_2352)
    {
        ok = fread(buf, 1, 2352, fp) == 2352;
    }
    else if(t->format == TRACK_MODE1_2048)
    {
        ok = fread(buf + 16, 1, 2048, fp) == 2048;
        if(ok) encode_mode1_sector((uint32_t)(lba + 150), buf);
    }
    else
        ok = false;
    fclose(fp);
    return ok;
}

bool CDIF_ReadRawSectorPWOnly_C(CDIF *cdif, uint8_t *pwbuf, int32_t lba, bool hint_fullread)
{
    (void)hint_fullread;
    if(!cdif || !pwbuf) return false;
    memset(pwbuf, 0, 96);
#ifdef PCFX_ENABLE_PHYSICAL_CD
    if(cdif->is_physical)
    {
        uint8_t tmpbuf[2352 + 96];
        if(lba < 0)
        {
            subpw_synth_udapp_lba(&cdif->disc_toc, lba, 0, pwbuf);
            return true;
        }
        if(lba >= cdif->total_sectors)
        {
            subpw_synth_leadout_lba(&cdif->disc_toc, lba, pwbuf);
            return true;
        }
        if(!CDPhys_ReadRawSector(cdif->physical, tmpbuf, lba))
            return false;
        memcpy(pwbuf, tmpbuf + 2352, 96);
        return true;
    }
#endif
    if(lba < 0)
    {
        subpw_synth_udapp_lba(&cdif->disc_toc, lba, 0, pwbuf);
        return true;
    }
    if(lba >= cdif->total_sectors)
    {
        subpw_synth_leadout_lba(&cdif->disc_toc, lba, pwbuf);
        return true;
    }
    make_subpq(cdif, lba, pwbuf);
    return true;
}

bool CDIF_ValidateRawSector_C(CDIF *cdif, uint8_t *buf)
{
    (void)cdif;
    if(!buf) return false;
    int mode = buf[12 + 3];
    if(mode != 0x1 && mode != 0x2)
        return false;
    /* The C11 port keeps validation non-mutating for raw/bin images; the source images already contain valid sectors. */
    return true;
}

int CDIF_ReadSector_C(CDIF *cdif, uint8_t *buf, int32_t lba, uint32_t sector_count, bool suppress_uncorrectable_message)
{
    (void)suppress_uncorrectable_message;
    int ret = 0;
    while(sector_count--)
    {
        uint8_t tmpbuf[2352 + 96];
        if(!CDIF_ReadRawSector_C(cdif, tmpbuf, lba))
            return 0;
        if(!CDIF_ValidateRawSector_C(cdif, tmpbuf))
            return 0;
        int mode = tmpbuf[12 + 3];
        if(!ret) ret = mode;
        if(mode == 1)
            memcpy(buf, tmpbuf + 16, 2048);
        else if(mode == 2)
            memcpy(buf, tmpbuf + 24, 2048);
        else
            return 0;
        buf += 2048;
        lba++;
    }
    return ret;
}
