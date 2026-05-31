#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "config.h"
#include "menu.h"

t_config option;
uint32_t emulator_state = 0;
uint32_t done = 0;
char home_path[2048];
char save_path[2048];
char sram_path[2048];
char conf_path[2048];

static void mkdir_if_missing(const char* path)
{
    if(path && path[0] && access(path, F_OK) == -1)
        mkdir(path, 0755);
}

extern "C" void pcfx_headless_set_paths(const char* bios_dir, const char* save_dir)
{
    const char* bd = (bios_dir && bios_dir[0]) ? bios_dir : ".";
    const char* sd = (save_dir && save_dir[0]) ? save_dir : bd;
    snprintf(home_path, sizeof(home_path), "%s", bd);
    snprintf(save_path, sizeof(save_path), "%s", sd);
    snprintf(sram_path, sizeof(sram_path), "%s", sd);
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

void Load_Configuration(void)
{
}

void Clean(void)
{
}

void Menu(void)
{
    emulator_state = 0;
}
