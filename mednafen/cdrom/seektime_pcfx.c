/*
 * PC-FX 2x CD-ROM seek-delay approximation.
 *
 * This is deliberately separate from the measured PC Engine CD-ROM2 model.
 * The PC-FX drive is a 2x CD-ROM unit, so transfer cadence and rotational
 * latency differ from the 1x PC Engine CD drive; sled/servo settle behavior is
 * assumed to be much closer.  The model therefore reuses the PCE spiral/zone
 * geometry but scales only the components that should plausibly differ.
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
#define PCFX_FRAME_MS          (1000.0f / 60.0f)
#define PCFX_ROTATION_SCALE_2X 0.50f
#define PCFX_LONG_SEEK_SCALE   0.70f

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

static float pcfx_rotation_ms(int target_group, float revolution_fraction)
{
    return pcfx_sector_groups[target_group].rotation_ms_1x * PCFX_ROTATION_SCALE_2X * revolution_fraction;
}

/*
 * Return synthetic PC-FX image-drive seek latency in milliseconds.
 *
 * Component policy:
 *   - the zone table and track-distance conversion are inherited from the
 *     measured PCE model because they describe CD spiral geometry;
 *   - short seeks and servo/data-stream settling are kept close to PCE;
 *   - long mechanical travel is reduced by 30% as a conservative stand-in for a
 *     newer/faster 2x-era PC-FX sled mechanism;
 *   - rotational latency is halved for 2x CLV.
 */
float PCFX_CDSeekMS(int start_sector, int target_sector)
{
    const int sector_delta = abs(target_sector - start_sector);
    const int target_group = pcfx_find_sector_group(target_sector);
    const float tracks = pcfx_track_delta(start_sector, target_sector);

    /* Adjacent sequential reads are assumed to remain in the PC-FX CD buffer/head stream.
       A one-track-or-more move still falls through to the short-seek minimum below. */
    if(sector_delta <= 1)
        return 0.0f;

    if(sector_delta <= 3)
        return 2.0f * PCFX_FRAME_MS;

    if(sector_delta < 7)
        return 9.0f * PCFX_FRAME_MS + pcfx_rotation_ms(target_group, 0.75f);

    if(tracks <= 80.0f)
        return 17.0f * PCFX_FRAME_MS + pcfx_rotation_ms(target_group, 0.75f);

    if(tracks <= 160.0f)
        return 22.0f * PCFX_FRAME_MS + pcfx_rotation_ms(target_group, 0.75f);

    if(tracks <= 644.0f)
    {
        const float long_motion_ms = (tracks - 161.0f) * (16.66f / 80.0f) * PCFX_LONG_SEEK_SCALE;
        return 22.0f * PCFX_FRAME_MS + pcfx_rotation_ms(target_group, 0.75f) + long_motion_ms;
    }
    else
    {
        const float long_entry_ms = (14.0f * PCFX_FRAME_MS) * PCFX_LONG_SEEK_SCALE;
        const float long_motion_ms = (tracks - 644.0f) * (16.66f / 195.0f) * PCFX_LONG_SEEK_SCALE;
        return 22.0f * PCFX_FRAME_MS + long_entry_ms + pcfx_rotation_ms(target_group, 0.50f) + long_motion_ms;
    }
}
