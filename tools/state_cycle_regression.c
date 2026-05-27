#include "headless/pcfx_headless.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static int fail(const char* msg, const PCFX_Headless* emu)
{
    fprintf(stderr, "%s%s%s\n", msg, emu ? ": " : "", emu ? pcfx_headless_last_error(emu) : "");
    return 0;
}

static int ensure_dir_cmd(const char* path)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
    return system(cmd) == 0;
}

static void run_frames_with_optional_pulses(PCFX_Headless* emu, unsigned frames, unsigned pulse_start, unsigned pulse_every, uint16_t pulse_button)
{
    for(unsigned f = 0; f < frames; f++)
    {
        uint16_t pad = 0;
        if(pulse_every && f >= pulse_start)
        {
            unsigned rel = f - pulse_start;
            if((rel % pulse_every) < 10)
                pad = pulse_button;
        }
        pcfx_headless_set_pad(emu, 0, pad);
        pcfx_headless_run_frame(emu);
    }
    pcfx_headless_set_pad(emu, 0, 0);
}

static int state_cycle(const char* label, const char* media, const char* bios_dir, const char* out_dir,
                       int prefer_fxga, unsigned pre_frames, unsigned post_frames,
                       unsigned pulse_start, unsigned pulse_every, uint16_t pulse_button,
                       char* state_out, size_t state_out_size)
{
    PCFX_HeadlessConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.bios_dir = bios_dir;
    cfg.save_dir = out_dir;
    cfg.sound_rate = 44100;
    cfg.prefer_fxga_bios = prefer_fxga;

    printf("%s: create\n", label); fflush(stdout);
    PCFX_Headless* emu = pcfx_headless_create(&cfg);
    if(!emu) return fail("create failed", NULL);
    printf("%s: load %s\n", label, media); fflush(stdout);
    if(!pcfx_headless_load_cd(emu, media)) { int r = fail("load failed", emu); pcfx_headless_destroy(emu); return r; }
    printf("%s: run pre %u\n", label, pre_frames); fflush(stdout);
    run_frames_with_optional_pulses(emu, pre_frames, pulse_start, pulse_every, pulse_button);

    char state[1024], p0[1024], p1[1024], p2[1024], p3[1024];
    snprintf(state, sizeof(state), "%s/%s.state", out_dir, label);
    snprintf(p0, sizeof(p0), "%s/%s_saved.ppm", out_dir, label);
    snprintf(p1, sizeof(p1), "%s/%s_played.ppm", out_dir, label);
    snprintf(p2, sizeof(p2), "%s/%s_reload_played.ppm", out_dir, label);
    snprintf(p3, sizeof(p3), "%s/%s_reload_again_played.ppm", out_dir, label);

    printf("%s: save state\n", label); fflush(stdout);
    if(!pcfx_headless_save_state(emu, state)) { int r = fail("save failed", emu); pcfx_headless_destroy(emu); return r; }
    if(!pcfx_headless_save_screenshot_ppm(emu, p0)) { int r = fail("screenshot save failed", emu); pcfx_headless_destroy(emu); return r; }

    printf("%s: run post %u\n", label, post_frames); fflush(stdout);
    run_frames_with_optional_pulses(emu, post_frames, 0, 0, 0);
    if(!pcfx_headless_save_screenshot_ppm(emu, p1)) { int r = fail("screenshot play failed", emu); pcfx_headless_destroy(emu); return r; }

    printf("%s: load state #1\n", label); fflush(stdout);
    if(!pcfx_headless_load_state(emu, state)) { int r = fail("reload failed", emu); pcfx_headless_destroy(emu); return r; }
    printf("%s: run post after reload #1 %u\n", label, post_frames); fflush(stdout);
    run_frames_with_optional_pulses(emu, post_frames, 0, 0, 0);
    if(!pcfx_headless_save_screenshot_ppm(emu, p2)) { int r = fail("screenshot reload failed", emu); pcfx_headless_destroy(emu); return r; }

    printf("%s: load state #2\n", label); fflush(stdout);
    if(!pcfx_headless_load_state(emu, state)) { int r = fail("reload-again failed", emu); pcfx_headless_destroy(emu); return r; }
    printf("%s: run post after reload #2 %u\n", label, post_frames); fflush(stdout);
    run_frames_with_optional_pulses(emu, post_frames, 0, 0, 0);
    if(!pcfx_headless_save_screenshot_ppm(emu, p3)) { int r = fail("screenshot reload-again failed", emu); pcfx_headless_destroy(emu); return r; }

    if(state_out && state_out_size)
        snprintf(state_out, state_out_size, "%s", state);
    printf("%s state_cycle ok frames=%llu state=%s\n", label, (unsigned long long)pcfx_headless_frame_count(emu), state);
    fflush(stdout);
    pcfx_headless_destroy(emu);
    return 1;
}

static int load_existing_state_cycle(const char* label, const char* media, const char* state, const char* bios_dir, const char* out_dir,
                                     int prefer_fxga, unsigned post_frames)
{
    PCFX_HeadlessConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.bios_dir = bios_dir;
    cfg.save_dir = out_dir;
    cfg.sound_rate = 44100;
    cfg.prefer_fxga_bios = prefer_fxga;
    PCFX_Headless* emu = pcfx_headless_create(&cfg);
    if(!emu) return fail("create failed", NULL);
    if(!pcfx_headless_load_cd(emu, media)) { int r = fail("load failed", emu); pcfx_headless_destroy(emu); return r; }
    if(!pcfx_headless_load_state(emu, state)) { int r = fail("state load failed", emu); pcfx_headless_destroy(emu); return r; }
    run_frames_with_optional_pulses(emu, post_frames, 0, 0, 0);
    char p[1024];
    snprintf(p, sizeof(p), "%s/%s.ppm", out_dir, label);
    if(!pcfx_headless_save_screenshot_ppm(emu, p)) { int r = fail("screenshot failed", emu); pcfx_headless_destroy(emu); return r; }
    printf("%s load_existing_state ok frames=%llu\n", label, (unsigned long long)pcfx_headless_frame_count(emu));
    fflush(stdout);
    pcfx_headless_destroy(emu);
    return 1;
}

int main(int argc, char** argv)
{
    if(argc < 8)
    {
        fprintf(stderr, "usage: %s BIOS_DIR OUT_DIR PSX_EX MAZE_EX SAMEGAME_CHD TEAM_CHD NNYUU_CHD\n", argv[0]);
        return 2;
    }
    const char* bios = argv[1];
    const char* out = argv[2];
    if(!ensure_dir_cmd(out)) return 2;

    char psx_state[1024] = {0};
    char team_state[1024] = {0};
    int ok = 1;
    ok &= state_cycle("psxdemo_ex", argv[3], bios, out, 1, 1200, 300, 0, 0, 0, psx_state, sizeof(psx_state));
    ok &= state_cycle("maze2d_ex", argv[4], bios, out, 1, 1200, 300, 0, 0, 0, NULL, 0);
    ok &= state_cycle("samegame", argv[5], bios, out, 0, 600, 180, 0, 0, 0, NULL, 0);
    ok &= state_cycle("teaminnocent", argv[6], bios, out, 0, 900, 180, 0, 0, 0, team_state, sizeof(team_state));
    ok &= state_cycle("nnyuu_pcfxga", argv[7], bios, out, 1, 1500, 180, 600, 180, PCFX_PAD_START, NULL, 0);

    /* Explicit media switching stress: EX -> Team Innocent -> EX state again. */
    if(psx_state[0] && team_state[0])
    {
        ok &= load_existing_state_cycle("switch_back_psxdemo_ex_state", argv[3], psx_state, bios, out, 1, 300);
        ok &= load_existing_state_cycle("switch_back_team_state", argv[6], team_state, bios, out, 0, 300);
    }
    return ok ? 0 : 1;
}
