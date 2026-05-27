#include "pcfx_headless.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Command
{
    uint64_t frame;
    bool relative;
    bool clear;
    uint16_t set_mask;
    uint16_t clear_mask;
};

struct Dump
{
    const char* region;
    const char* path;
};

struct CommandList
{
    struct Command* data;
    size_t count;
    size_t cap;
};

struct DumpList
{
    struct Dump* data;
    size_t count;
    size_t cap;
};

static void usage(const char* argv0)
{
    fprintf(stderr,
        "Usage: %s [options] [game.cue|game.chd|game.toc|game.bin|game.iso|playlist.m3u|program.ex]\n"
        "\n"
        "Options:\n"
        "  --bios-dir DIR          Directory or BIOS file; accepts pcfx.rom, pcfxbios.bin, pcfxv101.bin, pcfxga.rom (default: .)\n"
        "  --save-dir DIR          Directory for save/state side effects (default: bios dir)\n"
        "  --frames N              Run exactly N emulated frames (default: 1)\n"
        "  --commands FILE         Apply controller command list before each matching frame\n"
        "  --auto-run              Pulse RUN/START during early boot; useful for PC-FXGA BIOS CD prompts\n"
        "  --pad BUTTONS           Initial pad state, e.g. START+A or 0x0081\n"
        "  --screenshot FILE.ppm   Save final RGB screenshot as binary PPM\n"
        "  --y4m FILE.y4m          Capture every executed frame as YUV4MPEG2 C444\n"
        "  --wav FILE.wav          Capture stereo signed 16-bit PCM audio\n"
        "  --dump REGION FILE      Dump memory region: ram, saveram, or state\n"
        "  --state-in FILE         Load emulator state before running frames\n"
        "  --state-out FILE        Save emulator state after running frames\n"
        "  --fast-video            Use legacy fast RAINBOW backend instead of upstream-accurate backend\n"
        "  --disable-3d-hardware   Disable optional HuC6273/Aurora 3D chip\n"
        "  --auto                  Select PC-FX or PC-FXGA from the loaded media (default for frontends)\n"
        "  --pcfx                  Force standard PC-FX BIOS/startup path only\n"
        "  --pcfxga                Force PC-FXGA BIOS/startup path only\n"
        "\n"
        "Command file format:\n"
        "  # comments are allowed\n"
        "  0 START                 absolute state: START pressed at frame 0\n"
        "  30 NONE                 absolute state: no buttons at frame 30\n"
        "  60 +A +RIGHT            relative: press A and RIGHT\n"
        "  90 -A -RIGHT            relative: release A and RIGHT\n"
        "\n"
        "Button names: A B C X Y Z START SELECT UP DOWN LEFT RIGHT.\n",
        argv0);
}

static void uppercase_copy(char* dst, size_t dst_size, const char* src)
{
    if(!dst_size)
        return;
    size_t i = 0;
    if(src)
    {
        for(; i + 1 < dst_size && src[i]; i++)
            dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = 0;
}

static bool parse_button_atom(const char* token, uint16_t* bit)
{
    char t[64];
    uppercase_copy(t, sizeof(t), token ? token : "");
    if(!strcmp(t, "A")) *bit = PCFX_PAD_A;
    else if(!strcmp(t, "B")) *bit = PCFX_PAD_B;
    else if(!strcmp(t, "C")) *bit = PCFX_PAD_C;
    else if(!strcmp(t, "X")) *bit = PCFX_PAD_X;
    else if(!strcmp(t, "Y")) *bit = PCFX_PAD_Y;
    else if(!strcmp(t, "Z")) *bit = PCFX_PAD_Z;
    else if(!strcmp(t, "SELECT") || !strcmp(t, "SEL")) *bit = PCFX_PAD_SELECT;
    else if(!strcmp(t, "START") || !strcmp(t, "RUN")) *bit = PCFX_PAD_START;
    else if(!strcmp(t, "UP")) *bit = PCFX_PAD_UP;
    else if(!strcmp(t, "RIGHT")) *bit = PCFX_PAD_RIGHT;
    else if(!strcmp(t, "DOWN")) *bit = PCFX_PAD_DOWN;
    else if(!strcmp(t, "LEFT")) *bit = PCFX_PAD_LEFT;
    else return false;
    return true;
}

static bool parse_pad_expr(const char* expr, uint16_t* out)
{
    if(!expr || !out)
        return false;
    char* end = NULL;
    unsigned long numeric = strtoul(expr, &end, 0);
    if(end && *end == 0)
    {
        *out = (uint16_t)numeric;
        return true;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "%s", expr);
    uint16_t state = 0;
    const char* delims = "+,| \t\r\n";
    for(char* tok = strtok(buf, delims); tok; tok = strtok(NULL, delims))
    {
        uint16_t bit = 0;
        if(!parse_button_atom(tok, &bit))
            return false;
        state |= bit;
    }
    *out = state;
    return true;
}

static bool command_list_push(struct CommandList* list, struct Command cmd)
{
    if(list->count == list->cap)
    {
        size_t ncap = list->cap ? list->cap * 2 : 32;
        struct Command* ndata = (struct Command*)realloc(list->data, ncap * sizeof(*ndata));
        if(!ndata)
            return false;
        list->data = ndata;
        list->cap = ncap;
    }
    list->data[list->count++] = cmd;
    return true;
}

static bool dump_list_push(struct DumpList* list, struct Dump d)
{
    if(list->count == list->cap)
    {
        size_t ncap = list->cap ? list->cap * 2 : 8;
        struct Dump* ndata = (struct Dump*)realloc(list->data, ncap * sizeof(*ndata));
        if(!ndata)
            return false;
        list->data = ndata;
        list->cap = ncap;
    }
    list->data[list->count++] = d;
    return true;
}

static int command_cmp(const void* ap, const void* bp)
{
    const struct Command* a = (const struct Command*)ap;
    const struct Command* b = (const struct Command*)bp;
    if(a->frame < b->frame) return -1;
    if(a->frame > b->frame) return 1;
    return 0;
}

static bool load_commands(const char* path, struct CommandList* commands)
{
    FILE* fp = fopen(path, "rb");
    if(!fp)
    {
        fprintf(stderr, "Failed to open command file: %s\n", path);
        return false;
    }

    char line[1024];
    unsigned line_no = 0;
    while(fgets(line, sizeof(line), fp))
    {
        line_no++;
        char* hash = strchr(line, '#');
        if(hash) *hash = 0;
        char* tok = strtok(line, " \t\r\n");
        if(!tok)
            continue;
        char* end = NULL;
        uint64_t frame = strtoull(tok, &end, 0);
        if(!end || *end)
        {
            fprintf(stderr, "%s:%u: expected frame number\n", path, line_no);
            fclose(fp);
            return false;
        }

        struct Command cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.frame = frame;
        bool saw_token = false;
        bool saw_relative = false;
        bool saw_absolute = false;
        for(tok = strtok(NULL, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n"))
        {
            saw_token = true;
            char t[64];
            uppercase_copy(t, sizeof(t), tok);
            if(!strcmp(t, "NONE") || !strcmp(t, "CLEAR") || !strcmp(t, "0"))
            {
                cmd.clear = true;
                saw_absolute = true;
                continue;
            }
            char sign = 0;
            if(tok[0] == '+' || tok[0] == '-')
            {
                sign = tok[0];
                tok++;
                saw_relative = true;
            }
            else
                saw_absolute = true;

            uint16_t bit = 0;
            if(!parse_button_atom(tok, &bit))
            {
                uint16_t numeric = 0;
                if(parse_pad_expr(tok, &numeric))
                    bit = numeric;
                else
                {
                    fprintf(stderr, "%s:%u: unknown button '%s'\n", path, line_no, tok);
                    fclose(fp);
                    return false;
                }
            }
            if(sign == '-')
                cmd.clear_mask |= bit;
            else
                cmd.set_mask |= bit;
        }
        if(!saw_token)
        {
            fprintf(stderr, "%s:%u: expected at least one button token\n", path, line_no);
            fclose(fp);
            return false;
        }
        if(saw_relative && saw_absolute && !cmd.clear)
        {
            fprintf(stderr, "%s:%u: do not mix relative and absolute button syntax on one line\n", path, line_no);
            fclose(fp);
            return false;
        }
        cmd.relative = saw_relative && !cmd.clear;
        if(!command_list_push(commands, cmd))
        {
            fprintf(stderr, "out of memory while reading commands\n");
            fclose(fp);
            return false;
        }
    }
    fclose(fp);
    qsort(commands->data, commands->count, sizeof(commands->data[0]), command_cmp);
    return true;
}

static void apply_command(PCFX_Headless* emu, const struct Command* cmd)
{
    uint16_t state = pcfx_headless_get_pad(emu, 0);
    if(cmd->clear)
        state = 0;
    if(cmd->relative)
    {
        state |= cmd->set_mask;
        state &= (uint16_t)~cmd->clear_mask;
    }
    else
    {
        state = cmd->set_mask;
    }
    pcfx_headless_set_pad(emu, 0, state);
}


static bool add_auto_run_commands(struct CommandList* commands)
{
    static const struct { uint64_t frame; uint16_t state; } seq[] = {
        { 30, PCFX_PAD_START }, { 90, 0 },
        { 120, PCFX_PAD_START }, { 180, 0 },
        { 300, PCFX_PAD_START }, { 360, 0 },
        { 600, PCFX_PAD_START }, { 660, 0 }
    };
    for(size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
    {
        struct Command cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.frame = seq[i].frame;
        cmd.set_mask = seq[i].state;
        if(seq[i].state == 0)
            cmd.clear = true;
        if(!command_list_push(commands, cmd))
            return false;
    }
    return true;
}

static const char* need_arg(int* i, int argc, char** argv, const char* opt)
{
    if(*i + 1 >= argc)
    {
        fprintf(stderr, "%s requires an argument\n", opt);
        exit(2);
    }
    (*i)++;
    return argv[*i];
}

int main(int argc, char** argv)
{
    const char* bios_dir = ".";
    const char* save_dir = NULL;
    const char* screenshot = NULL;
    const char* y4m = NULL;
    const char* wav = NULL;
    const char* state_in = NULL;
    const char* state_out = NULL;
    const char* command_file = NULL;
    const char* game = NULL;
    uint64_t frames = 1;
    uint16_t initial_pad = 0;
    bool have_initial_pad = false;
    bool auto_run = false;
    bool fast_video = false;
    bool disable_3d_hardware = false;
    int bios_mode = 0;
    struct DumpList dumps;
    memset(&dumps, 0, sizeof(dumps));

    for(int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        if(!strcmp(a, "--help") || !strcmp(a, "-h"))
        {
            usage(argv[0]);
            free(dumps.data);
            return 0;
        }
        else if(!strcmp(a, "--bios-dir")) bios_dir = need_arg(&i, argc, argv, a);
        else if(!strcmp(a, "--save-dir")) save_dir = need_arg(&i, argc, argv, a);
        else if(!strcmp(a, "--frames")) frames = strtoull(need_arg(&i, argc, argv, a), NULL, 0);
        else if(!strcmp(a, "--commands")) command_file = need_arg(&i, argc, argv, a);
        else if(!strcmp(a, "--auto-run")) auto_run = true;
        else if(!strcmp(a, "--screenshot")) screenshot = need_arg(&i, argc, argv, a);
        else if(!strcmp(a, "--y4m")) y4m = need_arg(&i, argc, argv, a);
        else if(!strcmp(a, "--wav")) wav = need_arg(&i, argc, argv, a);
        else if(!strcmp(a, "--state-in")) state_in = need_arg(&i, argc, argv, a);
        else if(!strcmp(a, "--state-out")) state_out = need_arg(&i, argc, argv, a);
        else if(!strcmp(a, "--fast-video")) fast_video = true;
        else if(!strcmp(a, "--disable-3d-hardware")) disable_3d_hardware = true;
        else if(!strcmp(a, "--auto")) bios_mode = 2;
        else if(!strcmp(a, "--pcfx")) bios_mode = 0;
        else if(!strcmp(a, "--pcfxga")) bios_mode = 1;
        else if(!strcmp(a, "--pad"))
        {
            if(!parse_pad_expr(need_arg(&i, argc, argv, a), &initial_pad))
            {
                fprintf(stderr, "invalid --pad expression\n");
                free(dumps.data);
                return 2;
            }
            have_initial_pad = true;
        }
        else if(!strcmp(a, "--dump"))
        {
            struct Dump d;
            d.region = need_arg(&i, argc, argv, a);
            d.path = need_arg(&i, argc, argv, a);
            if(!dump_list_push(&dumps, d))
            {
                fprintf(stderr, "out of memory while adding dump request\n");
                free(dumps.data);
                return 2;
            }
        }
        else if(a[0] == '-')
        {
            fprintf(stderr, "Unknown option: %s\n", a);
            usage(argv[0]);
            free(dumps.data);
            return 2;
        }
        else if(!game)
            game = a;
        else
        {
            fprintf(stderr, "Unexpected positional argument: %s\n", a);
            free(dumps.data);
            return 2;
        }
    }

    struct CommandList commands;
    memset(&commands, 0, sizeof(commands));
    if(command_file && !load_commands(command_file, &commands))
    {
        free(dumps.data);
        free(commands.data);
        return 2;
    }
    if(auto_run && !add_auto_run_commands(&commands))
    {
        fprintf(stderr, "out of memory while adding auto-run commands\n");
        free(dumps.data);
        free(commands.data);
        return 2;
    }
    if(commands.count)
        qsort(commands.data, commands.count, sizeof(commands.data[0]), command_cmp);

    PCFX_HeadlessConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.bios_dir = bios_dir;
    cfg.save_dir = save_dir ? save_dir : bios_dir;
    cfg.sound_rate = 44100;
    cfg.fast_video = fast_video ? 1 : 0;
    cfg.disable_3d_hardware = disable_3d_hardware ? 1 : 0;
    cfg.prefer_fxga_bios = bios_mode;
    PCFX_Headless* emu = pcfx_headless_create(&cfg);
    if(!emu)
    {
        fprintf(stderr, "Failed to create headless emulator; another instance may already be active.\n");
        free(dumps.data);
        free(commands.data);
        return 1;
    }

    int status = 1;
    size_t next_cmd = 0;
    if(!pcfx_headless_load_cd(emu, game))
    {
        fprintf(stderr, "Load failed: %s\n", pcfx_headless_last_error(emu));
        goto out;
    }

    if(have_initial_pad)
        pcfx_headless_set_pad(emu, 0, initial_pad);
    if(state_in && !pcfx_headless_load_state(emu, state_in))
    {
        fprintf(stderr, "State load failed: %s\n", pcfx_headless_last_error(emu));
        goto out;
    }
    if(y4m && !pcfx_headless_open_y4m(emu, y4m))
    {
        fprintf(stderr, "Y4M open failed: %s\n", pcfx_headless_last_error(emu));
        goto out;
    }
    if(wav && !pcfx_headless_open_wav(emu, wav))
    {
        fprintf(stderr, "WAV open failed: %s\n", pcfx_headless_last_error(emu));
        goto out;
    }

    for(uint64_t f = 0; f < frames; f++)
    {
        while(next_cmd < commands.count && commands.data[next_cmd].frame == f)
            apply_command(emu, &commands.data[next_cmd++]);
        if(!pcfx_headless_run_frame(emu))
        {
            fprintf(stderr, "Run failed at frame %llu: %s\n", (unsigned long long)f, pcfx_headless_last_error(emu));
            goto out;
        }
    }

    if(screenshot && !pcfx_headless_save_screenshot_ppm(emu, screenshot))
    {
        fprintf(stderr, "Screenshot failed: %s\n", pcfx_headless_last_error(emu));
        goto out;
    }
    for(size_t di = 0; di < dumps.count; di++)
    {
        const struct Dump* d = &dumps.data[di];
        if(!pcfx_headless_dump_memory(emu, d->region, d->path))
        {
            fprintf(stderr, "Dump failed: %s\n", pcfx_headless_last_error(emu));
            goto out;
        }
    }
    if(state_out && !pcfx_headless_save_state(emu, state_out))
    {
        fprintf(stderr, "State save failed: %s\n", pcfx_headless_last_error(emu));
        goto out;
    }

    pcfx_headless_close_y4m(emu);
    pcfx_headless_close_wav(emu);
    fprintf(stdout, "ran_frames=%llu audio_frames=%llu final_pad=0x%04x video_backend=%s huc6273=%s\n",
            (unsigned long long)pcfx_headless_frame_count(emu),
            (unsigned long long)pcfx_headless_audio_frame_count(emu),
            pcfx_headless_get_pad(emu, 0),
            pcfx_headless_using_fast_video(emu) ? "fast" : "upstream",
            pcfx_headless_3d_hardware_enabled(emu) ? "enabled" : "disabled");
    status = 0;

out:
    pcfx_headless_destroy(emu);
    free(dumps.data);
    free(commands.data);
    return status;
}
