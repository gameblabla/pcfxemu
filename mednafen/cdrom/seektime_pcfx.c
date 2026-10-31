/*
 * PC-FX CD-ROM seek delay, calibrated against retail hardware.
 *
 * The PCE spiral/zone model supplies the head-travel curve; the Queen of Queens
 * hardware comparison calibrated its resulting latency to 0.6 for the PC-FX.
 * Keep this PC-FX-specific calibration here rather than changing the shared PCE
 * model used by PC Engine emulation.
 */
#include <stdlib.h>
#include <stdio.h>
#include "seektime_pcfx.h"

typedef struct
{
    int sec_per_revolution;
    int sec_start;
    int sec_end;
    float rotation_ms_1x;
} PCFXSectorGroup;

#define PCFX_NUM_SECTOR_GROUPS 14
#define PCFX_HARDWARE_CALIBRATION 0.60f

static const PCFXSectorGroup pcfx_sector_groups[PCFX_NUM_SECTOR_GROUPS] =
{
    { 10,      0,  12572, 133.47f },
    { 11,  12573,  30244, 146.82f },
    { 12,  30245,  49523, 160.17f },
    { 13,  49524,  70408, 173.51f },
    { 14,  70409,  92900, 186.86f },
    { 15,  92901, 116998, 200.21f },
    { 16, 116999, 142703, 213.56f },
    { 17, 142704, 170014, 226.90f },
    { 18, 170015, 198932, 240.25f },
    { 19, 198933, 229456, 253.60f },
    { 20, 229457, 261587, 266.95f },
    { 21, 261588, 295324, 280.29f },
    { 22, 295325, 330668, 293.64f },
    { 23, 330669, 333012, 306.99f }
};

static int pcfx_find_sector_group(int sector_num)
{
    if(sector_num < 0)
        sector_num = 0;

    for(int i = 0; i < PCFX_NUM_SECTOR_GROUPS; i++)
        if(sector_num >= pcfx_sector_groups[i].sec_start && sector_num <= pcfx_sector_groups[i].sec_end)
            return i;

    return PCFX_NUM_SECTOR_GROUPS - 1;
}

static float pcfx_track_delta(int start_sector, int target_sector)
{
    const int start_index = pcfx_find_sector_group(start_sector);
    const int target_index = pcfx_find_sector_group(target_sector);
    float track_difference;

    if(target_index == start_index)
    {
        track_difference = (float)abs(target_sector - start_sector) / (float)pcfx_sector_groups[target_index].sec_per_revolution;
    }
    else if(target_index > start_index)
    {
        track_difference = (float)(pcfx_sector_groups[start_index].sec_end - start_sector) / (float)pcfx_sector_groups[start_index].sec_per_revolution;
        track_difference += (float)(target_sector - pcfx_sector_groups[target_index].sec_start) / (float)pcfx_sector_groups[target_index].sec_per_revolution;
        track_difference += 1606.48f * (float)(target_index - start_index - 1);
    }
    else
    {
        track_difference = (float)(start_sector - pcfx_sector_groups[start_index].sec_start) / (float)pcfx_sector_groups[start_index].sec_per_revolution;
        track_difference += (float)(pcfx_sector_groups[target_index].sec_end - target_sector) / (float)pcfx_sector_groups[target_index].sec_per_revolution;
        track_difference += 1606.48f * (float)(start_index - target_index - 1);
    }

    if(track_difference < 0.0f)
        track_difference = -track_difference;
    return track_difference;
}

/*
 * Return the PCE head-travel curve scaled to the value that matched PC-FX
 * hardware in the Queen of Queens side-by-side comparison. The read-delay
 * plumbing applies this only to image-backed PC-FX drives.
 */
/* ---- measured seek model ---------------------------------------------------------------------
 * DIAG5 lab on a real PC-FX (NEC CD-ROM DRIVE:FX 1.1), 8-sector reads inside the 1350..17500 area.  Total
 * read time minus ~16 fields of command latency minus the transfer gives the positioning time below, in
 * fields (1/60 s).  The drive is MUCH slower than the earlier retail calibration (long forward seeks
 * 1.5-2.6 s; short backward seeks ~0.1 s; outward seeks slower than inward ones).  Points are
 * (sector distance, fields); linear in between.  PCFXEMU_CD_SEEK_OLD=1 selects the old calibration. */
typedef struct { float d; float f; } SeekNode;
static const SeekNode seek_fwd[] = {            /* target ahead of the head */
    { 0, 0 }, { 64, 0 }, { 90, 12 }, { 1000, 50 }, { 4000, 95 }, { 8000, 95 }, { 16100, 100 }, { 120000, 190 }, { 330000, 320 } };
static const SeekNode seek_back[] = {           /* target behind the head (also: re-reading what was just read) */
    { 0, 4.5f }, { 1, 4.5f }, { 300, 12 }, { 8000, 33 }, { 12300, 40 }, { 16200, 30 }, { 120000, 90 }, { 330000, 150 } };

static float seek_interp(const SeekNode *n, int count, float d)
{
    for(int i = 1; i < count; i++)
        if(d <= n[i].d)
            return n[i - 1].f + (n[i].f - n[i - 1].f) * (d - n[i - 1].d) / (n[i].d - n[i - 1].d);
    return n[count - 1].f;
}

static float PCFX_CDSeekMS_Old(int start_sector, int target_sector);

float PCFX_CDSeekMS(int start_sector, int target_sector)
{
    static int model = -1;
    if(model < 0)
    {
        const char *e = getenv("PCFXEMU_CD_SEEK_OLD");
        model = (e && e[0] && e[0] != '0') ? 0 : 1;
    }
    if(!model)
        return PCFX_CDSeekMS_Old(start_sector, target_sector);

    const int delta = target_sector - start_sector;
    float fields;
    if(delta > 0)
        fields = seek_interp(seek_fwd, (int)(sizeof(seek_fwd) / sizeof(seek_fwd[0])), (float)delta);
    else
        fields = seek_interp(seek_back, (int)(sizeof(seek_back) / sizeof(seek_back[0])), (float)(-delta));
    static float scale = -1.0f;
    if(scale < 0.0f) { const char *e = getenv("PCFXEMU_CD_SEEK_SCALE"); scale = (e && e[0]) ? (float)atof(e) / 100.0f : 0.90f; }
    return fields * scale * (1000.0f / 60.0f);
}

static float PCFX_CDSeekMS_Old(int start_sector, int target_sector)
{
    const int sector_delta = abs(target_sector - start_sector);
    const int target_group = pcfx_find_sector_group(target_sector);
    const float tracks = pcfx_track_delta(start_sector, target_sector);
    const float rotation_ms = pcfx_sector_groups[target_group].rotation_ms_1x;
    float milliseconds;

    if(sector_delta <= 3)
        milliseconds = (6.0f * 1000.0f / 60.0f) + rotation_ms * 0.75f;
    else if(sector_delta < 7)
        milliseconds = (9.0f * 1000.0f / 60.0f) + rotation_ms * 0.75f;
    else if(tracks <= 80.0f)
        milliseconds = (17.0f * 1000.0f / 60.0f) + rotation_ms * 0.75f;
    else if(tracks <= 160.0f)
        milliseconds = (22.0f * 1000.0f / 60.0f) + rotation_ms * 0.75f;
    else if(tracks <= 644.0f)
        milliseconds = (22.0f * 1000.0f / 60.0f) + rotation_ms * 0.75f +
                       (tracks - 161.0f) * 16.66f / 80.0f;
    else
        milliseconds = (36.0f * 1000.0f / 60.0f) + rotation_ms * 0.50f +
                       (tracks - 644.0f) * 16.66f / 195.0f;

    return milliseconds * PCFX_HARDWARE_CALIBRATION;
}
