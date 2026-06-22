#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "config.h"
#include "menu.h"

t_config option;
uint32_t emulator_state = 0;
uint32_t done = 0;
char home_path[2048];
char save_path[2048];
char sram_path[2048];
char conf_path[2048];

extern char GameName_emu[256];
extern void SRAM_Save(char* path, uint_fast8_t state);
static bool sram_loaded_for_game;

static void mkdir_if_missing(const char* path)
{
    if(path && path[0] && access(path, F_OK) == -1)
        mkdir(path, 0755);
}

static int file_exists_fe(const char* path)
{
    return path && path[0] && access(path, F_OK) == 0;
}

static void join_path_fe(char* out, size_t out_size, const char* dir, const char* leaf)
{
    size_t len;
    if(!out || !out_size)
        return;
    if(!dir || !dir[0])
    {
        snprintf(out, out_size, "%s", leaf ? leaf : "");
        return;
    }
    len = strlen(dir);
    if(dir[len - 1] == '/' || dir[len - 1] == '\\')
        snprintf(out, out_size, "%s%s", dir, leaf ? leaf : "");
    else
        snprintf(out, out_size, "%s/%s", dir, leaf ? leaf : "");
}

void pcfx_headless_set_paths(const char* bios_dir, const char* save_dir)
{
    const char* bd = (bios_dir && bios_dir[0]) ? bios_dir : ".";
    const char* sd = (save_dir && save_dir[0]) ? save_dir : bd;
    snprintf(home_path, sizeof(home_path), "%s", bd);
    snprintf(save_path, sizeof(save_path), "%s", sd);
    join_path_fe(sram_path, sizeof(sram_path), sd, "sram");
    snprintf(conf_path, sizeof(conf_path), "%s", sd);
    mkdir_if_missing(home_path);
    mkdir_if_missing(save_path);
    mkdir_if_missing(sram_path);
    mkdir_if_missing(conf_path);

    memset(&option, 0, sizeof(option));
    option.fullscreen = 0;
    option.type_controller = 0;
}

void Init_Configuration(void)
{
    pcfx_headless_set_paths(".", ".");
}

static int make_sram_path_in_dir(char* out, size_t out_size, const char* dir)
{
    char file[512];
    if(!out || !out_size || !GameName_emu[0])
        return 0;
    snprintf(file, sizeof(file), "%s.srm", GameName_emu);
    join_path_fe(out, out_size, dir && dir[0] ? dir : ".", file);
    return 1;
}

static int make_sram_path(char* out, size_t out_size)
{
    return make_sram_path_in_dir(out, out_size, sram_path[0] ? sram_path : ".");
}

static int make_legacy_sram_path(char* out, size_t out_size)
{
    return make_sram_path_in_dir(out, out_size, save_path[0] ? save_path : ".");
}

void Load_Configuration(void)
{
    char path[2048];
    char legacy_path[2048];
    sram_loaded_for_game = false;
    if(make_sram_path(path, sizeof(path)))
    {
        /* New layout: NVRAM/SRAM lives under <save-root>/sram.
         * Backward compatibility: if a previous fixed build wrote the .srm
         * directly in <save-root>, load it once and save back to the new
         * canonical sram/ directory on Clean().
         */
        if(!file_exists_fe(path) && make_legacy_sram_path(legacy_path, sizeof(legacy_path)) && file_exists_fe(legacy_path))
            SRAM_Save(legacy_path, 1);
        else
            SRAM_Save(path, 1);
        sram_loaded_for_game = true;
    }
}

void Clean(void)
{
    char path[2048];
    if(sram_loaded_for_game && make_sram_path(path, sizeof(path)))
        SRAM_Save(path, 0);
    sram_loaded_for_game = false;
}

void Menu(void)
{
    emulator_state = 0;
}
