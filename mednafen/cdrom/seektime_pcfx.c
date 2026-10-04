/*
 * PC-FX CD-ROM seek delay, calibrated against retail hardware.
 *
 * The PCE spiral/zone model supplies the head-travel curve; the Queen of Queens
 * hardware comparison calibrated its resulting latency to 0.6 for the PC-FX.
 * Keep this PC-FX-specific calibration here rather than changing the shared PCE
 * model used by PC Engine emulation.
 */
#include <stdlib.h>
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
float PCFX_CDSeekMS(int start_sector, int target_sector)
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
