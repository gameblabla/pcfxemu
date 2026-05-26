#include "pcfx_headless.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

struct Command
{
    uint64_t frame;
    bool relative;
    bool clear;
    uint16_t set_mask;
    uint16_t clear_mask;
};

static void usage(const char* argv0)
{
    fprintf(stderr,
        "Usage: %s [options] game.cue|game.chd|game.ccd|game.toc|playlist.m3u|program.ex\n"
        "\n"
        "Options:\n"
        "  --bios-dir DIR          Directory containing pcfx.rom (default: .)\n"
        "  --save-dir DIR          Directory for save/state side effects (default: bios dir)\n"
        "  --frames N              Run exactly N emulated frames (default: 1)\n"
        "  --commands FILE         Apply controller command list before each matching frame\n"
        "  --pad BUTTONS           Initial pad state, e.g. START+A or 0x0081\n"
        "  --screenshot FILE.ppm   Save final RGB screenshot as binary PPM\n"
        "  --y4m FILE.y4m          Capture every executed frame as YUV4MPEG2 C444\n"
        "  --wav FILE.wav          Capture stereo signed 16-bit PCM audio\n"
        "  --dump REGION FILE      Dump memory region: ram, saveram, or state\n"
        "  --state-in FILE         Load emulator state before running frames\n"
        "  --state-out FILE        Save emulator state after running frames\n"
        "  --fast-video            Use legacy fast RAINBOW backend instead of upstream-accurate backend\n"
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

static std::string upper(std::string s)
{
    for(char& c : s)
        c = (char)toupper((unsigned char)c);
    return s;
}

static bool parse_button_atom(const char* token, uint16_t* bit)
{
    std::string t = upper(token ? token : "");
    if(t == "A") *bit = PCFX_PAD_A;
    else if(t == "B") *bit = PCFX_PAD_B;
    else if(t == "C") *bit = PCFX_PAD_C;
    else if(t == "X") *bit = PCFX_PAD_X;
    else if(t == "Y") *bit = PCFX_PAD_Y;
    else if(t == "Z") *bit = PCFX_PAD_Z;
    else if(t == "SELECT" || t == "SEL") *bit = PCFX_PAD_SELECT;
    else if(t == "START" || t == "RUN") *bit = PCFX_PAD_START;
    else if(t == "UP") *bit = PCFX_PAD_UP;
    else if(t == "RIGHT") *bit = PCFX_PAD_RIGHT;
    else if(t == "DOWN") *bit = PCFX_PAD_DOWN;
    else if(t == "LEFT") *bit = PCFX_PAD_LEFT;
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
    const char* delims = "+,| 	\r\n";
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

static bool load_commands(const char* path, std::vector<Command>* commands)
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

        Command cmd = {};
        cmd.frame = frame;
        bool saw_token = false;
        bool saw_relative = false;
        bool saw_absolute = false;
        for(tok = strtok(NULL, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n"))
        {
            saw_token = true;
            std::string t = upper(tok);
            if(t == "NONE" || t == "CLEAR" || t == "0")
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
        commands->push_back(cmd);
    }
    fclose(fp);
    std::sort(commands->begin(), commands->end(), [](const Command& a, const Command& b) { return a.frame < b.frame; });
    return true;
}

static void apply_command(PCFX_Headless* emu, const Command& cmd)
{
    uint16_t state = pcfx_headless_get_pad(emu, 0);
    if(cmd.clear)
        state = 0;
    if(cmd.relative)
    {
        state |= cmd.set_mask;
        state &= (uint16_t)~cmd.clear_mask;
    }
    else
    {
        state = cmd.set_mask;
    }
    pcfx_headless_set_pad(emu, 0, state);
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
    bool fast_video = false;
    struct Dump { const char* region; const char* path; };
    std::vector<Dump> dumps;

    for(int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        auto need = [&](const char* opt) -> const char* {
            if(i + 1 >= argc)
            {
                fprintf(stderr, "%s requires an argument\n", opt);
                exit(2);
            }
            return argv[++i];
        };
        if(!strcmp(a, "--help") || !strcmp(a, "-h"))
        {
            usage(argv[0]);
            return 0;
        }
        else if(!strcmp(a, "--bios-dir")) bios_dir = need(a);
        else if(!strcmp(a, "--save-dir")) save_dir = need(a);
        else if(!strcmp(a, "--frames")) frames = strtoull(need(a), NULL, 0);
        else if(!strcmp(a, "--commands")) command_file = need(a);
        else if(!strcmp(a, "--screenshot")) screenshot = need(a);
        else if(!strcmp(a, "--y4m")) y4m = need(a);
        else if(!strcmp(a, "--wav")) wav = need(a);
        else if(!strcmp(a, "--state-in")) state_in = need(a);
        else if(!strcmp(a, "--state-out")) state_out = need(a);
        else if(!strcmp(a, "--fast-video")) fast_video = true;
        else if(!strcmp(a, "--pad"))
        {
            if(!parse_pad_expr(need(a), &initial_pad))
            {
                fprintf(stderr, "invalid --pad expression\n");
                return 2;
            }
            have_initial_pad = true;
        }
        else if(!strcmp(a, "--dump"))
        {
            Dump d;
            d.region = need(a);
            d.path = need(a);
            dumps.push_back(d);
        }
        else if(a[0] == '-')
        {
            fprintf(stderr, "Unknown option: %s\n", a);
            usage(argv[0]);
            return 2;
        }
        else if(!game)
            game = a;
        else
        {
            fprintf(stderr, "Unexpected positional argument: %s\n", a);
            return 2;
        }
    }

    if(!game)
    {
        usage(argv[0]);
        return 2;
    }

    std::vector<Command> commands;
    if(command_file && !load_commands(command_file, &commands))
        return 2;

    PCFX_HeadlessConfig cfg = {};
    cfg.bios_dir = bios_dir;
    cfg.save_dir = save_dir ? save_dir : bios_dir;
    cfg.sound_rate = 44100;
    cfg.fast_video = fast_video ? 1 : 0;
    PCFX_Headless* emu = pcfx_headless_create(&cfg);
    if(!emu)
    {
        fprintf(stderr, "Failed to create headless emulator; another instance may already be active.\n");
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
        while(next_cmd < commands.size() && commands[next_cmd].frame == f)
            apply_command(emu, commands[next_cmd++]);
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
    for(const Dump& d : dumps)
    {
        if(!pcfx_headless_dump_memory(emu, d.region, d.path))
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
    fprintf(stdout, "ran_frames=%llu audio_frames=%llu final_pad=0x%04x video_backend=%s\n",
            (unsigned long long)pcfx_headless_frame_count(emu),
            (unsigned long long)pcfx_headless_audio_frame_count(emu),
            pcfx_headless_get_pad(emu, 0),
            pcfx_headless_using_fast_video(emu) ? "fast" : "upstream");
    status = 0;

out:
    pcfx_headless_destroy(emu);
    return status;
}
