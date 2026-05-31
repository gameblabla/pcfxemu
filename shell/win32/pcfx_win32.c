#define STRICT
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <xinput.h>
#include <direct.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "menu.h"
#include "config.h"
#include "video_blit.h"
#include "input_emu.h"
#include "input_win32.h"
#include "sound_output.h"
#include "sound_output_win32.h"
#include "resource.h"
#include "mednafen/cdrom/cdromif.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define APP_CLASS_NAME "PCFXEmuWin32"
#define MAP_CLASS_NAME "PCFXEmuWin32MapDialog"
#define PCFX_WIN32_CONFIG_VERSION 9

#if defined(PCFX_WIN32_HAVE_D3D11)
#define PCFX_WIN32_DEFAULT_VIDEO_BACKEND PCFX_WIN32_VIDEO_D3D11
#else
#define PCFX_WIN32_DEFAULT_VIDEO_BACKEND PCFX_WIN32_VIDEO_GDI
#endif

#if defined(PCFX_WIN32_HAVE_WASAPI)
#define PCFX_WIN32_AUDIO_BACKEND_MAX PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE
#else
#define PCFX_WIN32_AUDIO_BACKEND_MAX PCFX_WIN32_AUDIO_WAVEOUT
#endif

#define ID_FILE_LOAD             100
#define ID_FILE_BOOT_BIOS        101
#define ID_FILE_LOAD_PHYSICAL    104
#define ID_FILE_CLOSE            102
#define ID_FILE_EXIT             103
#define ID_SYSTEM_RESET          200
#define ID_SYSTEM_MODE_PCFX      210
#define ID_SYSTEM_MODE_FXGA      211
#define ID_SYSTEM_MODE_AUTO      212
#define ID_SYSTEM_HUC6273        213
#define ID_SYSTEM_PATCH_SHORTINTRO 220
#define ID_SYSTEM_PATCH_ENGLISH    221
#define ID_SYSTEM_PATCH_AUTOLAUNCH 222
#define ID_STATE_CURRENT         250
#define ID_STATE_LOAD            251
#define ID_STATE_SAVE            252
#define ID_STATE_SLOT_PREV       253
#define ID_STATE_SLOT_NEXT       254
#define ID_STATE_SLOT_0          260
#define ID_STATE_SLOT_1          261
#define ID_STATE_SLOT_2          262
#define ID_STATE_SLOT_3          263
#define ID_STATE_SLOT_4          264
#define ID_STATE_SLOT_5          265
#define ID_STATE_SLOT_6          266
#define ID_STATE_SLOT_7          267
#define ID_STATE_SLOT_8          268
#define ID_STATE_SLOT_9          269
#define ID_VIDEO_SCALE_1X        300
#define ID_VIDEO_SCALE_2X        301
#define ID_VIDEO_SCALE_3X        302
#define ID_VIDEO_SCALE_4X        303
#define ID_VIDEO_SCALE_5X        304
#define ID_VIDEO_SCALE_6X        305
#define ID_VIDEO_MODE_FIXED      309
#define ID_VIDEO_STRETCH         310
#define ID_VIDEO_KEEP_ASPECT     311
#define ID_VIDEO_INTEGER         312
#define ID_VIDEO_SMOOTH          313
#define ID_VIDEO_FULLSCREEN      314
#define ID_VIDEO_BACKEND_D3D11   315
#define ID_VIDEO_BACKEND_GDI     316
#define ID_VIDEO_FS_EXCLUSIVE    317
#define ID_VIDEO_FS_BORDERLESS   318
#define ID_AUDIO_WAVEOUT         350
#define ID_AUDIO_WASAPI_SHARED   351
#define ID_AUDIO_WASAPI_EXCLUSIVE 352
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
#define ID_AUDIO_ADPCM_BUGGY_AUTO 360
#define ID_AUDIO_ADPCM_BUGGY_OFF  361
#define ID_AUDIO_ADPCM_BUGGY_ON   362
#define ID_AUDIO_ADPCM_SUPPRESS_CLICKS 363
#endif
#define ID_AUDIO_CD_SPEED_1X    370
#define ID_AUDIO_CD_SPEED_2X    371
#define ID_AUDIO_CD_SPEED_4X    372
#define ID_AUDIO_CD_SPEED_8X    373
#define ID_AUDIO_CD_SPEED_16X   374
#define ID_INPUT_CONFIGURE       400
#define ID_INPUT_PORT1_PAD       401
#define ID_INPUT_PORT1_MOUSE     402
#define ID_INPUT_XINPUT_ENABLED  403
#define ID_INPUT_HOTKEYS         404
#define ID_HELP_ABOUT            500

#define ID_MAP_BASE              1000
#define ID_MAP_OK                2000
#define ID_MAP_DEFAULTS          2001
#define ID_MAP_CANCEL            2002

#define ID_HOTKEY_KEY_OPEN       2100
#define ID_HOTKEY_PAD_OPEN       2101
#define ID_HOTKEY_PAD_PLAYER     2102
#define ID_HOTKEY_OK             2110
#define ID_HOTKEY_DEFAULTS       2111
#define ID_HOTKEY_CANCEL         2112
#define ID_HOTKEY_TIMER_XINPUT   2
#define WM_PCFX_OPEN_GAME         (WM_APP + 1)

/* menu.c is not linked in the Win32 build, so this frontend owns these symbols. */
t_config option;
uint32_t emulator_state = 0;
char home_path[2048], save_path[2048], sram_path[2048], conf_path[2048];

extern uint8_t exit_vb;
extern char GameName_emu[256];

static HINSTANCE g_hinst;
static HWND g_hwnd;
static HMENU g_menu;
static HMENU g_window_menu;
static HMENU g_state_menu;
static HMENU g_bios_patch_menu;
static HMENU g_audio_menu;
static HMENU g_adpcm_buggy_menu;
static int g_game_loaded;
static int g_audio_open;
static int g_window_active = 1;
static int g_window_minimized;
static int g_menu_mute_depth;
static int g_modal_mute_depth;
static int g_transition_mute_depth;
static int g_effective_audio_muted = -1;
static int g_core_initialized;
static int g_system_mode = 2; /* 0=PC-FX, 1=FXGA, 2=Auto */
static int g_huc6273_enabled = 1;
static uint32_t g_bios_patch_flags = 0;
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
static int g_adpcm_buggy_codec_mode = PCFX_ADPCM_BUGGY_AUTO;
static int g_adpcm_suppress_reset_clicks = 1;
#endif
static int g_cd_speed = 2;
static int g_save_slot = 0;
static int g_fullscreen;
static int g_start_fullscreen;
static int g_launchbox_mode;
static uint32_t g_open_hotkey_key = VK_F10;
static int g_open_hotkey_xinput_player = 0;
static uint32_t g_open_hotkey_xinput_code = XINPUT_GAMEPAD_RIGHT_THUMB;
static int g_open_hotkey_pad_prev;
static int g_open_hotkey_dialog_active;
static int g_launchbox_exit_combo_prev;
static LARGE_INTEGER g_launchbox_exit_hold_start;
static WINDOWPLACEMENT g_window_placement = { sizeof(WINDOWPLACEMENT) };
static DWORD g_window_style;
static DWORD g_window_exstyle;
static char g_exe_dir[2048];
static char g_common_doc_dir[2048];
static char g_ini_path[2048];
static int g_config_repair_needed;

static LARGE_INTEGER g_qpc_freq;
static LARGE_INTEGER g_next_frame;


static void update_audio_mute_state(void)
{
    int muted = (!g_window_active || g_window_minimized ||
                 g_menu_mute_depth > 0 || g_modal_mute_depth > 0 ||
                 g_transition_mute_depth > 0 || emulator_state != 0) ? 1 : 0;
    if(!g_game_loaded)
        muted = 1;
    if(muted != g_effective_audio_muted)
    {
        PCFX_Win32_AudioSetMuted(muted);
        g_effective_audio_muted = muted;
    }
}

static void audio_modal_mute_begin(void)
{
    g_modal_mute_depth++;
    update_audio_mute_state();
}

static void audio_modal_mute_end(void)
{
    if(g_modal_mute_depth > 0)
        g_modal_mute_depth--;
    update_audio_mute_state();
}

static void audio_transition_mute_begin(void)
{
    g_transition_mute_depth++;
    update_audio_mute_state();
}

static void audio_transition_mute_end(void)
{
    if(g_transition_mute_depth > 0)
        g_transition_mute_depth--;
    update_audio_mute_state();
}

static int pcfx_message_box(HWND hwnd, const char* text, const char* caption, UINT type)
{
    int ret;
    audio_modal_mute_begin();
    ret = MessageBoxA(hwnd, text, caption, type);
    audio_modal_mute_end();
    return ret;
}


static void safe_copy(char* dst, size_t dst_size, const char* src)
{
    if(!dst || !dst_size) return;
    if(!src) src = "";
    snprintf(dst, dst_size, "%s", src);
}

static void path_dirname_win32(const char* path, char* out, size_t out_size)
{
    safe_copy(out, out_size, path ? path : "");
    char* slash = strrchr(out, '\\');
    char* fslash = strrchr(out, '/');
    if(!slash || (fslash && fslash > slash)) slash = fslash;
    if(slash) *slash = 0;
    if(!out[0]) safe_copy(out, out_size, ".");
}

static const char* path_basename_win32(const char* path)
{
    if(!path) return "";
    const char* a = strrchr(path, '\\');
    const char* b = strrchr(path, '/');
    const char* p = a;
    if(b && (!p || b > p)) p = b;
    return p ? p + 1 : path;
}

static void path_parent_win32(const char* path, char* out, size_t out_size)
{
    char tmp[2048];
    path_dirname_win32(path, tmp, sizeof(tmp));
    path_dirname_win32(tmp, out, out_size);
}

static void mkdir_one(const char* path)
{
    if(path && path[0])
        _mkdir(path);
}

static int file_exists(const char* path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int parse_int_string(const char* text, int* out)
{
    if(!text || !text[0])
        return 0;

    char* end = NULL;
    long v = strtol(text, &end, 0);
    if(end == text)
        return 0;
    while(*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        end++;
    if(*end)
        return 0;
    if(v < -2147483647L - 1L || v > 2147483647L)
        return 0;
    if(out)
        *out = (int)v;
    return 1;
}

static int read_ini_int_range(const char* section, const char* key, int def_value, int min_value, int max_value)
{
    char buf[96];
    DWORD len = GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), g_ini_path);
    if(len == 0)
        return def_value;

    int value = def_value;
    if(!parse_int_string(buf, &value))
    {
        g_config_repair_needed = 1;
        return def_value;
    }
    if(value < min_value)
    {
        g_config_repair_needed = 1;
        return min_value;
    }
    if(value > max_value)
    {
        g_config_repair_needed = 1;
        return max_value;
    }
    return value;
}

static int config_version_allows_load(void)
{
    if(!file_exists(g_ini_path))
        return 1;

    char buf[96];
    DWORD len = GetPrivateProfileStringA("Config", "Version", "", buf, sizeof(buf), g_ini_path);
    if(len == 0)
    {
        /* Legacy config.  Load it through the compatibility path, then rewrite
         * it with the current version so future validation is explicit. */
        g_config_repair_needed = 1;
        return 1;
    }

    int version = 0;
    if(!parse_int_string(buf, &version) || version < 1 || version > PCFX_WIN32_CONFIG_VERSION)
    {
        char backup[2300];
        snprintf(backup, sizeof(backup), "%s.invalid", g_ini_path);
        CopyFileA(g_ini_path, backup, FALSE);
        g_config_repair_needed = 1;
        return 0;
    }

    return 1;
}

static void join_path(char* out, size_t out_size, const char* a, const char* b)
{
    if(!a || !a[0]) a = ".";
    if(!b) b = "";
    size_t n = strlen(a);
    const char sep = (n && (a[n - 1] == '\\' || a[n - 1] == '/')) ? '\0' : '\\';
    if(sep)
        snprintf(out, out_size, "%s\\%s", a, b);
    else
        snprintf(out, out_size, "%s%s", a, b);
}

static void ensure_frontend_dirs(void)
{
    mkdir_one(home_path);
    mkdir_one(conf_path);
    mkdir_one(save_path);
    mkdir_one(sram_path);
}

static int folder_has_bios_kind(const char* folder, int fxga)
{
    static const char* const console_names[] = {
        "pcfx.rom", "pcfxbios.bin", "pcfxv101.bin", "pcfx_bios.bin",
        "PCFX.ROM", "PCFXBIOS.BIN", "PCFXV101.BIN"
    };
    static const char* const fxga_names[] = {
        "pcfxga.rom", "pcfxga.bin", "PCFXGA.ROM", "PCFXGA.BIN"
    };
    const char* const* names = fxga ? fxga_names : console_names;
    size_t count = fxga ? ARRAY_SIZE(fxga_names) : ARRAY_SIZE(console_names);
    char path[2048];
    for(size_t i = 0; i < count; i++)
    {
        join_path(path, sizeof(path), folder, names[i]);
        if(file_exists(path))
            return 1;
    }
    return 0;
}

static int folder_has_bios_for_mode(const char* folder)
{
    if(g_system_mode == 1)
        return folder_has_bios_kind(folder, 1);
    if(g_system_mode == 0)
        return folder_has_bios_kind(folder, 0);
    return folder_has_bios_kind(folder, 0) || folder_has_bios_kind(folder, 1);
}

static int select_bios_root(char* out, size_t out_size)
{
    if(folder_has_bios_for_mode(g_exe_dir))
    {
        safe_copy(out, out_size, g_exe_dir);
        return 1;
    }
    if(g_common_doc_dir[0] && folder_has_bios_for_mode(g_common_doc_dir))
    {
        safe_copy(out, out_size, g_common_doc_dir);
        return 1;
    }
    safe_copy(out, out_size, g_exe_dir);
    return 0;
}

static void show_missing_bios_message(HWND parent)
{
    char msg[4096];
    const char* need = (g_system_mode == 1) ? "PC-FXGA BIOS: pcfxga.rom or pcfxga.bin" :
                       (g_system_mode == 0) ? "PC-FX BIOS: pcfx.rom, pcfxbios.bin, pcfxv101.bin, or pcfx_bios.bin" :
                       "PC-FX BIOS or PC-FXGA BIOS, depending on the loaded media";
    snprintf(msg, sizeof(msg),
        "No suitable BIOS was found.\n\n"
        "Needed for the current system mode: %s\n\n"
        "Place the BIOS next to pcfx.exe:\n%s\n\n"
        "or in the shared documents folder:\n%s\n\n"
        "The core requires a 1 MB BIOS image; if the file exists but is still rejected, check that it is the correct dump.",
        need,
        g_exe_dir[0] ? g_exe_dir : ".",
        g_common_doc_dir[0] ? g_common_doc_dir : "Public Documents\\PCFXEmu");
    pcfx_message_box(parent, msg, "PCFXEmu - BIOS missing", MB_ICONERROR | MB_OK);
}

static void set_frontend_root_from_bios_search(void)
{
    char root[2048];
    select_bios_root(root, sizeof(root));
    safe_copy(home_path, sizeof(home_path), root);
    join_path(conf_path, sizeof(conf_path), home_path, "conf");
    join_path(save_path, sizeof(save_path), home_path, "sstates");
    join_path(sram_path, sizeof(sram_path), home_path, "sram");
    ensure_frontend_dirs();
    join_path(g_ini_path, sizeof(g_ini_path), conf_path, "pcfx_win32.ini");
}

static void set_game_name_from_path(const char* path)
{
    if(CDIF_IsPhysicalPath_C(path))
    {
        safe_copy(GameName_emu, sizeof(GameName_emu), "Physical CD-ROM");
        return;
    }
    const char* base = path_basename_win32(path);
    safe_copy(GameName_emu, sizeof(GameName_emu), base);
}

static void sram_menu(uint_fast8_t load_mode)
{
    if(!GameName_emu[0])
        return;
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s/%s.srm", sram_path, GameName_emu);
    SRAM_Save(tmp, load_mode);
}

static void save_config(void)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%d", PCFX_WIN32_CONFIG_VERSION);
    WritePrivateProfileStringA("Config", "Version", tmp, g_ini_path);
#if defined(_WIN64)
    WritePrivateProfileStringA("Config", "Arch", "x64", g_ini_path);
#else
    WritePrivateProfileStringA("Config", "Arch", "x86-old", g_ini_path);
#endif
    WritePrivateProfileStringA("Config", "PixelFormat", PCFX_Win32_GetPixelFormatName(), g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", g_system_mode);
    WritePrivateProfileStringA("System", "Mode", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", g_huc6273_enabled);
    WritePrivateProfileStringA("System", "HuC6273", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)g_bios_patch_flags);
    WritePrivateProfileStringA("System", "BIOSPatches", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", g_save_slot);
    WritePrivateProfileStringA("State", "Slot", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", option.type_controller);
    WritePrivateProfileStringA("Input", "Port1Device", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", PCFX_Win32_InputGetXInputEnabled());
    WritePrivateProfileStringA("Input", "XInputEnabled", tmp, g_ini_path);
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        char key[32];
        snprintf(key, sizeof(key), "P%d_XInputUser", p + 1);
        snprintf(tmp, sizeof(tmp), "%d", PCFX_Win32_InputGetXInputUser(p));
        WritePrivateProfileStringA("Input", key, tmp, g_ini_path);
    }
    snprintf(tmp, sizeof(tmp), "%d", PCFX_Win32_AudioGetBackend());
    WritePrivateProfileStringA("Audio", "Backend", tmp, g_ini_path);
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
    snprintf(tmp, sizeof(tmp), "%d", g_adpcm_buggy_codec_mode);
    WritePrivateProfileStringA("Audio", "ADPCMBuggyCodecMode", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", g_adpcm_suppress_reset_clicks);
    WritePrivateProfileStringA("Audio", "ADPCMSuppressChannelResetClicks", tmp, g_ini_path);
#endif
    snprintf(tmp, sizeof(tmp), "%d", g_cd_speed);
    WritePrivateProfileStringA("CDROM", "Speed", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)g_open_hotkey_key);
    WritePrivateProfileStringA("Hotkeys", "OpenGameKey", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", g_open_hotkey_xinput_player);
    WritePrivateProfileStringA("Hotkeys", "OpenGameXInputPlayer", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)g_open_hotkey_xinput_code);
    WritePrivateProfileStringA("Hotkeys", "OpenGameXInputCode", tmp, g_ini_path);

    int scale, mode, smooth;
    PCFX_Win32_GetScaleMode(&scale, &mode, &smooth);
    snprintf(tmp, sizeof(tmp), "%d", scale); WritePrivateProfileStringA("Video", "Scale", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", mode); WritePrivateProfileStringA("Video", "ScaleMode", tmp, g_ini_path);

    /* Keep the old keys readable for older test builds/configs, but derive
     * them from one authoritative mode so impossible combinations are never
     * written again. */
    snprintf(tmp, sizeof(tmp), "%d", mode == PCFX_WIN32_SCALE_STRETCH); WritePrivateProfileStringA("Video", "Stretch", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", mode == PCFX_WIN32_SCALE_ASPECT); WritePrivateProfileStringA("Video", "KeepAspect", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", mode == PCFX_WIN32_SCALE_INTEGER); WritePrivateProfileStringA("Video", "IntegerScale", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", smooth); WritePrivateProfileStringA("Video", "Smooth", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", PCFX_Win32_GetVideoBackend()); WritePrivateProfileStringA("Video", "Backend", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", PCFX_Win32_GetFullscreenMode()); WritePrivateProfileStringA("Video", "FullscreenMode", tmp, g_ini_path);
    snprintf(tmp, sizeof(tmp), "%d", g_fullscreen); WritePrivateProfileStringA("Video", "Fullscreen", tmp, g_ini_path);

    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        for(int b = 0; b < PCFX_WIN32_BUTTONS; b++)
        {
            char key[32];
            snprintf(key, sizeof(key), "P%d_%02d", p + 1, b);
            snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)PCFX_Win32_InputGetMapping(p, b));
            WritePrivateProfileStringA("Keys", key, tmp, g_ini_path);
            snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)PCFX_Win32_InputGetXInputMapping(p, b));
            WritePrivateProfileStringA("XInput", key, tmp, g_ini_path);
        }
    }
}

static void load_config(void)
{
    g_config_repair_needed = 0;
    PCFX_Win32_InputDefaults();

    const int config_usable = config_version_allows_load();

    /* Defaults.  Keep-aspect is the intended first-run video mode. */
    g_system_mode = 2;
    g_huc6273_enabled = 1;
    g_bios_patch_flags = 0;
    g_save_slot = 0;
    option.type_controller = 0;
    option.fullscreen = 0;
    g_start_fullscreen = 0;
    PCFX_Win32_InputSetXInputEnabled(1);
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
        PCFX_Win32_InputSetXInputUser(p, p);
    PCFX_Win32_AudioSetBackend(PCFX_WIN32_AUDIO_WAVEOUT);
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
    g_adpcm_buggy_codec_mode = PCFX_ADPCM_BUGGY_AUTO;
    g_adpcm_suppress_reset_clicks = 1;
#endif
    g_cd_speed = 2;
    g_open_hotkey_key = VK_F10;
    g_open_hotkey_xinput_player = 0;
    g_open_hotkey_xinput_code = XINPUT_GAMEPAD_RIGHT_THUMB;
    PCFX_Win32_SetScaleMode(2, PCFX_WIN32_SCALE_ASPECT, 0);
    PCFX_Win32_SetVideoBackend(PCFX_WIN32_DEFAULT_VIDEO_BACKEND);
    PCFX_Win32_SetFullscreenMode(PCFX_WIN32_FULLSCREEN_EXCLUSIVE);

    if(!config_usable)
    {
        save_config();
        return;
    }

    g_system_mode = read_ini_int_range("System", "Mode", 2, 0, 2);
    g_huc6273_enabled = read_ini_int_range("System", "HuC6273", 1, 0, 1) ? 1 : 0;
    g_bios_patch_flags = (uint32_t)read_ini_int_range("System", "BIOSPatches", 0, 0,
        (int)(PCFX_BIOS_PATCH_SHORTINTRO | PCFX_BIOS_PATCH_ENGLISH | PCFX_BIOS_PATCH_AUTOLAUNCH));
    g_bios_patch_flags &= (PCFX_BIOS_PATCH_SHORTINTRO | PCFX_BIOS_PATCH_ENGLISH | PCFX_BIOS_PATCH_AUTOLAUNCH);
    g_save_slot = read_ini_int_range("State", "Slot", 0, 0, 9);
    option.type_controller = (uint8_t)(read_ini_int_range("Input", "Port1Device", 0, 0, 1) ? 1 : 0);
    PCFX_Win32_InputSetXInputEnabled(read_ini_int_range("Input", "XInputEnabled", 1, 0, 1));
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        char key[32];
        snprintf(key, sizeof(key), "P%d_XInputUser", p + 1);
        PCFX_Win32_InputSetXInputUser(p, read_ini_int_range("Input", key, p, 0, PCFX_WIN32_XINPUT_USERS - 1));
    }
    g_start_fullscreen = read_ini_int_range("Video", "Fullscreen", 0, 0, 1) ? 1 : 0;
    PCFX_Win32_AudioSetBackend(read_ini_int_range("Audio", "Backend", PCFX_WIN32_AUDIO_WAVEOUT,
                                                  PCFX_WIN32_AUDIO_WAVEOUT, PCFX_WIN32_AUDIO_BACKEND_MAX));
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
    g_adpcm_buggy_codec_mode = read_ini_int_range("Audio", "ADPCMBuggyCodecMode", PCFX_ADPCM_BUGGY_AUTO,
                                                  PCFX_ADPCM_BUGGY_AUTO, PCFX_ADPCM_BUGGY_ON);
    if(GetPrivateProfileIntA("Audio", "ADPCMBuggyCodecMode", -9999, g_ini_path) == -9999)
    {
        /* Compatibility with the previous boolean key. */
        int old_bool = GetPrivateProfileIntA("Audio", "ADPCMEmulateBuggyCodec", -1, g_ini_path);
        if(old_bool >= 0)
            g_adpcm_buggy_codec_mode = old_bool ? PCFX_ADPCM_BUGGY_ON : PCFX_ADPCM_BUGGY_OFF;
    }
    g_adpcm_suppress_reset_clicks = read_ini_int_range("Audio", "ADPCMSuppressChannelResetClicks", 1, 0, 1) ? 1 : 0;
#endif
    g_cd_speed = read_ini_int_range("CDROM", "Speed", 2, 1, 16);
    if(g_cd_speed != 1 && g_cd_speed != 2 && g_cd_speed != 4 && g_cd_speed != 8 && g_cd_speed != 16)
    {
        g_config_repair_needed = 1;
        g_cd_speed = 2;
    }
    g_open_hotkey_key = (uint32_t)read_ini_int_range("Hotkeys", "OpenGameKey", VK_F10, 0, 0xFFFF);
    g_open_hotkey_xinput_player = read_ini_int_range("Hotkeys", "OpenGameXInputPlayer", 0, 0, PCFX_WIN32_PLAYERS - 1);
    g_open_hotkey_xinput_code = (uint32_t)read_ini_int_range("Hotkeys", "OpenGameXInputCode", XINPUT_GAMEPAD_RIGHT_THUMB, 0, 0x0001FFFF);

    int scale = read_ini_int_range("Video", "Scale", 2, 1, 6);
    int mode = read_ini_int_range("Video", "ScaleMode", PCFX_WIN32_SCALE_ASPECT,
                                  PCFX_WIN32_SCALE_FIXED, PCFX_WIN32_SCALE_INTEGER);
    int smooth = read_ini_int_range("Video", "Smooth", 0, 0, 1);

    /* Legacy compatibility: only consult the old independent booleans when
     * ScaleMode is absent.  Fresh configs default to keep-aspect. */
    char mode_buf[32];
    if(GetPrivateProfileStringA("Video", "ScaleMode", "", mode_buf, sizeof(mode_buf), g_ini_path) == 0)
    {
        int stretch = read_ini_int_range("Video", "Stretch", 0, 0, 1);
        int aspect = read_ini_int_range("Video", "KeepAspect", 1, 0, 1);
        int integer_scale = read_ini_int_range("Video", "IntegerScale", 0, 0, 1);
        if(integer_scale)
            mode = PCFX_WIN32_SCALE_INTEGER;
        else if(aspect)
            mode = PCFX_WIN32_SCALE_ASPECT;
        else if(stretch)
            mode = PCFX_WIN32_SCALE_STRETCH;
        else
            mode = PCFX_WIN32_SCALE_ASPECT;
    }
    PCFX_Win32_SetScaleMode(scale, mode, smooth);
    {
        int backend = read_ini_int_range("Video", "Backend", PCFX_WIN32_DEFAULT_VIDEO_BACKEND,
                                         PCFX_WIN32_VIDEO_D3D11, PCFX_WIN32_VIDEO_GDI);
        PCFX_Win32_SetVideoBackend(backend);
    }
#if defined(PCFX_WIN32_HAVE_D3D11)
    PCFX_Win32_SetFullscreenMode(read_ini_int_range("Video", "FullscreenMode",
                                                     PCFX_WIN32_FULLSCREEN_EXCLUSIVE,
                                                     PCFX_WIN32_FULLSCREEN_EXCLUSIVE,
                                                     PCFX_WIN32_FULLSCREEN_BORDERLESS));
#else
    PCFX_Win32_SetFullscreenMode(PCFX_WIN32_FULLSCREEN_BORDERLESS);
#endif

    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        for(int b = 0; b < PCFX_WIN32_BUTTONS; b++)
        {
            char key[32];
            snprintf(key, sizeof(key), "P%d_%02d", p + 1, b);
            UINT vk = (UINT)read_ini_int_range("Keys", key, (int)PCFX_Win32_InputGetMapping(p, b), 0, 0xFFFF);
            PCFX_Win32_InputSetMapping(p, b, vk);
            UINT xcode = (UINT)read_ini_int_range("XInput", key, (int)PCFX_Win32_InputGetXInputMapping(p, b), 0, 0x0001FFFF);
            PCFX_Win32_InputSetXInputMapping(p, b, xcode);
        }
    }

    if(g_config_repair_needed)
        save_config();
}

void Init_Configuration(void)
{
    char module[MAX_PATH];
    GetModuleFileNameA(NULL, module, sizeof(module));
    path_dirname_win32(module, g_exe_dir, sizeof(g_exe_dir));

    char common[MAX_PATH];
    common[0] = 0;
    if(SHGetFolderPathA(NULL, CSIDL_COMMON_DOCUMENTS, NULL, SHGFP_TYPE_CURRENT, common) == S_OK)
    {
        join_path(g_common_doc_dir, sizeof(g_common_doc_dir), common, "PCFXEmu");
        mkdir_one(g_common_doc_dir);
    }

    set_frontend_root_from_bios_search();
    load_config();
}

void Load_Configuration(void)
{
    sram_menu(1);
}

void Clean(void)
{
    if(g_game_loaded)
        sram_menu(0);
    save_config();
}

static void apply_core_options(void)
{
    PCFX_SetSystemMode(g_system_mode);
    PCFX_SetBIOSPatches(g_bios_patch_flags);
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
    PCFX_SetADPCMCompatOptions(g_adpcm_buggy_codec_mode,
                               g_adpcm_suppress_reset_clicks ? true : false);
#endif
    PCFX_SetCDSpeed((uint_fast32_t)g_cd_speed);
#ifdef HAVE_HUC6273
    PCFX_SetHuC6273Enabled(g_huc6273_enabled ? true : false);
#else
    (void)g_huc6273_enabled;
#endif
}

static void update_menu_checks(void)
{
    int scale, mode, smooth;
    PCFX_Win32_GetScaleMode(&scale, &mode, &smooth);
    CheckMenuRadioItem(g_menu, ID_SYSTEM_MODE_PCFX, ID_SYSTEM_MODE_AUTO,
                       g_system_mode == 0 ? ID_SYSTEM_MODE_PCFX : g_system_mode == 1 ? ID_SYSTEM_MODE_FXGA : ID_SYSTEM_MODE_AUTO,
                       MF_BYCOMMAND);
    CheckMenuItem(g_menu, ID_SYSTEM_HUC6273, MF_BYCOMMAND | (g_huc6273_enabled ? MF_CHECKED : MF_UNCHECKED));
    if(g_bios_patch_menu)
    {
        CheckMenuItem(g_bios_patch_menu, ID_SYSTEM_PATCH_SHORTINTRO, MF_BYCOMMAND | ((g_bios_patch_flags & PCFX_BIOS_PATCH_SHORTINTRO) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(g_bios_patch_menu, ID_SYSTEM_PATCH_ENGLISH, MF_BYCOMMAND | ((g_bios_patch_flags & PCFX_BIOS_PATCH_ENGLISH) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(g_bios_patch_menu, ID_SYSTEM_PATCH_AUTOLAUNCH, MF_BYCOMMAND | ((g_bios_patch_flags & PCFX_BIOS_PATCH_AUTOLAUNCH) ? MF_CHECKED : MF_UNCHECKED));
    }
    if(g_state_menu)
    {
        char slot_text[64];
        snprintf(slot_text, sizeof(slot_text), "Current slot: %d", g_save_slot);
        ModifyMenuA(g_state_menu, ID_STATE_CURRENT, MF_BYCOMMAND | MF_STRING | MF_GRAYED, ID_STATE_CURRENT, slot_text);
    }
    if(g_state_menu)
        CheckMenuRadioItem(g_state_menu, ID_STATE_SLOT_0, ID_STATE_SLOT_9, ID_STATE_SLOT_0 + g_save_slot, MF_BYCOMMAND);
    CheckMenuRadioItem(g_menu, ID_VIDEO_SCALE_1X, ID_VIDEO_SCALE_6X,
                       scale <= 1 ? ID_VIDEO_SCALE_1X :
                       scale == 2 ? ID_VIDEO_SCALE_2X :
                       scale == 3 ? ID_VIDEO_SCALE_3X :
                       scale == 4 ? ID_VIDEO_SCALE_4X :
                       scale == 5 ? ID_VIDEO_SCALE_5X : ID_VIDEO_SCALE_6X,
                       MF_BYCOMMAND);
    CheckMenuRadioItem(g_menu, ID_VIDEO_MODE_FIXED, ID_VIDEO_INTEGER,
                       mode == PCFX_WIN32_SCALE_STRETCH ? ID_VIDEO_STRETCH :
                       mode == PCFX_WIN32_SCALE_ASPECT ? ID_VIDEO_KEEP_ASPECT :
                       mode == PCFX_WIN32_SCALE_INTEGER ? ID_VIDEO_INTEGER : ID_VIDEO_MODE_FIXED,
                       MF_BYCOMMAND);
    CheckMenuItem(g_menu, ID_VIDEO_SMOOTH, MF_BYCOMMAND | (smooth ? MF_CHECKED : MF_UNCHECKED));
#if defined(PCFX_WIN32_HAVE_D3D11)
    CheckMenuRadioItem(g_menu, ID_VIDEO_BACKEND_D3D11, ID_VIDEO_BACKEND_GDI,
                       PCFX_Win32_GetVideoBackend() == PCFX_WIN32_VIDEO_GDI ? ID_VIDEO_BACKEND_GDI : ID_VIDEO_BACKEND_D3D11,
                       MF_BYCOMMAND);
    CheckMenuRadioItem(g_menu, ID_VIDEO_FS_EXCLUSIVE, ID_VIDEO_FS_BORDERLESS,
                       PCFX_Win32_GetFullscreenMode() == PCFX_WIN32_FULLSCREEN_BORDERLESS ? ID_VIDEO_FS_BORDERLESS : ID_VIDEO_FS_EXCLUSIVE,
                       MF_BYCOMMAND);
#endif
    CheckMenuItem(g_menu, ID_VIDEO_FULLSCREEN, MF_BYCOMMAND | (g_fullscreen ? MF_CHECKED : MF_UNCHECKED));
#if defined(PCFX_WIN32_HAVE_WASAPI)
    CheckMenuRadioItem(g_menu, ID_AUDIO_WAVEOUT, ID_AUDIO_WASAPI_EXCLUSIVE,
                       ID_AUDIO_WAVEOUT + PCFX_Win32_AudioGetBackend(),
                       MF_BYCOMMAND);
#else
    CheckMenuItem(g_menu, ID_AUDIO_WAVEOUT, MF_BYCOMMAND | MF_CHECKED);
#endif
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
    if(g_adpcm_buggy_menu)
        CheckMenuRadioItem(g_adpcm_buggy_menu, ID_AUDIO_ADPCM_BUGGY_AUTO, ID_AUDIO_ADPCM_BUGGY_ON,
                           g_adpcm_buggy_codec_mode == PCFX_ADPCM_BUGGY_ON ? ID_AUDIO_ADPCM_BUGGY_ON :
                           g_adpcm_buggy_codec_mode == PCFX_ADPCM_BUGGY_OFF ? ID_AUDIO_ADPCM_BUGGY_OFF : ID_AUDIO_ADPCM_BUGGY_AUTO,
                           MF_BYCOMMAND);
    CheckMenuItem(g_menu, ID_AUDIO_ADPCM_SUPPRESS_CLICKS,
                  MF_BYCOMMAND | (g_adpcm_suppress_reset_clicks ? MF_CHECKED : MF_UNCHECKED));
#endif
    if(g_audio_menu)
    {
        UINT cd_id = g_cd_speed <= 1 ? ID_AUDIO_CD_SPEED_1X :
                     g_cd_speed <= 2 ? ID_AUDIO_CD_SPEED_2X :
                     g_cd_speed <= 4 ? ID_AUDIO_CD_SPEED_4X :
                     g_cd_speed <= 8 ? ID_AUDIO_CD_SPEED_8X : ID_AUDIO_CD_SPEED_16X;
        CheckMenuRadioItem(g_audio_menu, ID_AUDIO_CD_SPEED_1X, ID_AUDIO_CD_SPEED_16X, cd_id, MF_BYCOMMAND);
    }
    CheckMenuRadioItem(g_menu, ID_INPUT_PORT1_PAD, ID_INPUT_PORT1_MOUSE,
                       option.type_controller ? ID_INPUT_PORT1_MOUSE : ID_INPUT_PORT1_PAD,
                       MF_BYCOMMAND);
    CheckMenuItem(g_menu, ID_INPUT_XINPUT_ENABLED, MF_BYCOMMAND | (PCFX_Win32_InputGetXInputEnabled() ? MF_CHECKED : MF_UNCHECKED));
}

static void force_video_redraw(HWND hwnd)
{
    if(!hwnd)
        return;
    RECT rc;
    if(GetClientRect(hwnd, &rc))
        PCFX_Win32_SetClientSize(rc.right - rc.left, rc.bottom - rc.top);
    PCFX_Win32_ForceRedraw();
    ValidateRect(hwnd, NULL);
}

static DWORD sanitize_windowed_style(DWORD style)
{
    /* Never persist or restore a fullscreen/borderless style as the normal
     * window style. If the current HWND was left as WS_POPUP by an interrupted
     * fullscreen transition, restore with the canonical decorated emulator
     * window style instead. */
    if((style & WS_POPUP) || !(style & WS_CAPTION) || !(style & WS_THICKFRAME))
        return WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    return style | WS_VISIBLE;
}

static DWORD sanitize_windowed_exstyle(DWORD style, DWORD exstyle)
{
    if((style & WS_POPUP) || !(style & WS_CAPTION) || !(style & WS_THICKFRAME))
        return 0;
    return exstyle;
}


static void set_save_slot(HWND hwnd, int slot)
{
    if(slot < 0) slot = 0;
    if(slot > 9) slot = 9;
    g_save_slot = slot;
    update_menu_checks();
    save_config();

    (void)hwnd;
}

static void change_save_slot(HWND hwnd, int delta)
{
    int slot = g_save_slot + delta;
    if(slot < 0) slot = 9;
    if(slot > 9) slot = 0;
    set_save_slot(hwnd, slot);
}

static void state_menu_action(HWND hwnd, uint_fast8_t load_mode)
{
    if(!g_game_loaded || !GameName_emu[0])
    {
        pcfx_message_box(hwnd, "No game is running.", "PCFXEmu - Save states", MB_ICONINFORMATION | MB_OK);
        return;
    }

    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s/%s_%d.sts", save_path, GameName_emu, g_save_slot);
    bool ok = SaveState(tmp, load_mode);
    if(ok)
    {
        if(load_mode)
            force_video_redraw(hwnd);
    }
    else
    {
        char msg[384];
        snprintf(msg, sizeof(msg), "Could not %s state slot %d.\n\n%s",
                 load_mode ? "load" : "save", g_save_slot, tmp);
        pcfx_message_box(hwnd, msg, "PCFXEmu - Save states", MB_ICONERROR | MB_OK);
    }
}

static void toggle_bios_patch(HWND hwnd, uint32_t flag)
{
    g_bios_patch_flags ^= flag;
    g_bios_patch_flags &= (PCFX_BIOS_PATCH_SHORTINTRO | PCFX_BIOS_PATCH_ENGLISH | PCFX_BIOS_PATCH_AUTOLAUNCH);
    PCFX_SetBIOSPatches(g_bios_patch_flags);
    update_menu_checks();
    save_config();

    if(g_game_loaded)
    {
        pcfx_message_box(hwnd,
                    "BIOS patch options will be applied on the next soft reset or BIOS/game boot.\n\n"
                    "Soft Reset reloads the BIOS image from disk, reapplies the selected in-memory patches, and then resets the emulated machine.\n"
                    "The BIOS file on disk is never modified.",
                    "PCFXEmu - BIOS patches",
                    MB_ICONINFORMATION | MB_OK);
    }
}

static HMENU build_menu(void)
{
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU system = CreatePopupMenu();
    HMENU state = CreatePopupMenu();
    HMENU video = CreatePopupMenu();
    HMENU audio = CreatePopupMenu();
    HMENU input = CreatePopupMenu();
    HMENU help = CreatePopupMenu();

    AppendMenuA(file, MF_STRING, ID_FILE_LOAD, "&Load Disc / EXE...\tCtrl+O");
#ifdef PCFX_ENABLE_PHYSICAL_CD
    AppendMenuA(file, MF_STRING, ID_FILE_LOAD_PHYSICAL, "Load &Physical CD-ROM");
#endif
    AppendMenuA(file, MF_STRING, ID_FILE_BOOT_BIOS, "&Boot BIOS");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_FILE_CLOSE, "&Close");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_FILE_EXIT, "E&xit");

    AppendMenuA(system, MF_STRING, ID_SYSTEM_RESET, "&Soft Reset\tF5");
    AppendMenuA(system, MF_SEPARATOR, 0, NULL);
    AppendMenuA(system, MF_STRING, ID_SYSTEM_MODE_AUTO, "BIOS mode: &Auto");
    AppendMenuA(system, MF_STRING, ID_SYSTEM_MODE_PCFX, "BIOS mode: PC-&FX");
    AppendMenuA(system, MF_STRING, ID_SYSTEM_MODE_FXGA, "BIOS mode: PC-FX&GA");
    AppendMenuA(system, MF_SEPARATOR, 0, NULL);
    AppendMenuA(system, MF_STRING, ID_SYSTEM_HUC6273, "Enable HuC6273 / Aurora 3D chip");
    AppendMenuA(system, MF_SEPARATOR, 0, NULL);
    HMENU patches = CreatePopupMenu();
    g_bios_patch_menu = patches;
    AppendMenuA(patches, MF_STRING, ID_SYSTEM_PATCH_SHORTINTRO, "&Short intro / faster boot");
    AppendMenuA(patches, MF_STRING, ID_SYSTEM_PATCH_ENGLISH, "Partial &English BIOS menus");
    AppendMenuA(patches, MF_STRING, ID_SYSTEM_PATCH_AUTOLAUNCH, "&Auto-launch CD after intro");
    AppendMenuA(system, MF_POPUP, (UINT_PTR)patches, "Original PC-FX BIOS in-memory &patches");

    g_state_menu = state;
    AppendMenuA(state, MF_STRING | MF_GRAYED, ID_STATE_CURRENT, "Current slot: 0");
    AppendMenuA(state, MF_SEPARATOR, 0, NULL);
    AppendMenuA(state, MF_STRING, ID_STATE_LOAD, "&Load State\tF7");
    AppendMenuA(state, MF_STRING, ID_STATE_SAVE, "&Save State\tF8");
    AppendMenuA(state, MF_SEPARATOR, 0, NULL);
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_PREV, "&Previous Slot\tF6");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_NEXT, "&Next Slot\tF9");
    AppendMenuA(state, MF_SEPARATOR, 0, NULL);
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_0, "Slot &0");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_1, "Slot &1");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_2, "Slot &2");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_3, "Slot &3");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_4, "Slot &4");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_5, "Slot &5");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_6, "Slot &6");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_7, "Slot &7");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_8, "Slot &8");
    AppendMenuA(state, MF_STRING, ID_STATE_SLOT_9, "Slot &9");

    AppendMenuA(video, MF_STRING, ID_VIDEO_SCALE_1X, "Window size &1x");
    AppendMenuA(video, MF_STRING, ID_VIDEO_SCALE_2X, "Window size &2x");
    AppendMenuA(video, MF_STRING, ID_VIDEO_SCALE_3X, "Window size &3x");
    AppendMenuA(video, MF_STRING, ID_VIDEO_SCALE_4X, "Window size &4x");
    AppendMenuA(video, MF_STRING, ID_VIDEO_SCALE_5X, "Window size &5x");
    AppendMenuA(video, MF_STRING, ID_VIDEO_SCALE_6X, "Window size &6x");
    AppendMenuA(video, MF_SEPARATOR, 0, NULL);
    AppendMenuA(video, MF_STRING, ID_VIDEO_MODE_FIXED, "&Fixed window size");
    AppendMenuA(video, MF_STRING, ID_VIDEO_STRETCH, "&Stretch to window");
    AppendMenuA(video, MF_STRING, ID_VIDEO_KEEP_ASPECT, "Keep &aspect ratio");
    AppendMenuA(video, MF_STRING, ID_VIDEO_INTEGER, "&Integer scale to window");
    AppendMenuA(video, MF_STRING, ID_VIDEO_SMOOTH, "&Smooth stretch/filtering");
    AppendMenuA(video, MF_SEPARATOR, 0, NULL);
#if defined(PCFX_WIN32_HAVE_D3D11)
    HMENU video_backend = CreatePopupMenu();
    AppendMenuA(video_backend, MF_STRING, ID_VIDEO_BACKEND_D3D11, "&D3D11 (hardware accelerated)");
    AppendMenuA(video_backend, MF_STRING, ID_VIDEO_BACKEND_GDI, "&GDI fallback");
    AppendMenuA(video, MF_POPUP, (UINT_PTR)video_backend, "Video &backend");

    HMENU fullscreen_mode = CreatePopupMenu();
    AppendMenuA(fullscreen_mode, MF_STRING, ID_VIDEO_FS_EXCLUSIVE, "&Exclusive display mode (D3D11)");
    AppendMenuA(fullscreen_mode, MF_STRING, ID_VIDEO_FS_BORDERLESS, "&Borderless desktop fallback");
    AppendMenuA(video, MF_POPUP, (UINT_PTR)fullscreen_mode, "Fullscreen &mode");
    AppendMenuA(video, MF_SEPARATOR, 0, NULL);
#endif
    AppendMenuA(video, MF_STRING, ID_VIDEO_FULLSCREEN, "&Fullscreen\tF11 / Alt+Enter");

    g_audio_menu = audio;
    AppendMenuA(audio, MF_STRING, ID_AUDIO_WAVEOUT, "&WaveOut");
#if defined(PCFX_WIN32_HAVE_WASAPI)
    AppendMenuA(audio, MF_STRING, ID_AUDIO_WASAPI_SHARED, "WASAPI &shared");
    AppendMenuA(audio, MF_STRING, ID_AUDIO_WASAPI_EXCLUSIVE, "WASAPI &exclusive");
#endif
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
    AppendMenuA(audio, MF_SEPARATOR, 0, NULL);
    HMENU adpcm_buggy = CreatePopupMenu();
    g_adpcm_buggy_menu = adpcm_buggy;
    AppendMenuA(adpcm_buggy, MF_STRING, ID_AUDIO_ADPCM_BUGGY_AUTO, "&Auto: Miraculum only");
    AppendMenuA(adpcm_buggy, MF_STRING, ID_AUDIO_ADPCM_BUGGY_OFF, "&Off");
    AppendMenuA(adpcm_buggy, MF_STRING, ID_AUDIO_ADPCM_BUGGY_ON, "&On");
    AppendMenuA(audio, MF_POPUP, (UINT_PTR)adpcm_buggy, "ADPCM: emulate buggy encoder codec");
    AppendMenuA(audio, MF_STRING, ID_AUDIO_ADPCM_SUPPRESS_CLICKS, "ADPCM: suppress channel reset clicks");
#endif
    AppendMenuA(audio, MF_SEPARATOR, 0, NULL);
    AppendMenuA(audio, MF_STRING, ID_AUDIO_CD_SPEED_1X, "CD-ROM speed: &1x");
    AppendMenuA(audio, MF_STRING, ID_AUDIO_CD_SPEED_2X, "CD-ROM speed: &2x");
    AppendMenuA(audio, MF_STRING, ID_AUDIO_CD_SPEED_4X, "CD-ROM speed: &4x");
    AppendMenuA(audio, MF_STRING, ID_AUDIO_CD_SPEED_8X, "CD-ROM speed: &8x");
    AppendMenuA(audio, MF_STRING, ID_AUDIO_CD_SPEED_16X, "CD-ROM speed: 1&6x");

    AppendMenuA(input, MF_STRING, ID_INPUT_CONFIGURE, "&Configure Controllers...");
    AppendMenuA(input, MF_STRING, ID_INPUT_HOTKEYS, "Configure &Hotkeys...");
    AppendMenuA(input, MF_STRING, ID_INPUT_XINPUT_ENABLED, "Enable &XInput gamepads");
    AppendMenuA(input, MF_SEPARATOR, 0, NULL);
    AppendMenuA(input, MF_STRING, ID_INPUT_PORT1_PAD, "Port 1: &Pad");
    AppendMenuA(input, MF_STRING, ID_INPUT_PORT1_MOUSE, "Port 1: &Mouse");

    AppendMenuA(help, MF_STRING, ID_HELP_ABOUT, "&About");

    AppendMenuA(menu, MF_POPUP, (UINT_PTR)file, "&File");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)system, "&System");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)state, "S&tate");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)video, "&Video");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)audio, "&Audio");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)input, "&Input");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)help, "&Help");
    return menu;
}

static void toggle_fullscreen(HWND hwnd);

static void resize_for_scale(int scale)
{
    if(!g_hwnd)
        return;

    if(scale < 1) scale = 1;
    if(scale > 6) scale = 6;

    RECT rc = {0, 0, PCFX_Win32_GetDisplayWidth() * scale, PCFX_Win32_GetDisplayHeight() * scale};
    DWORD style = (DWORD)GetWindowLongPtrA(g_hwnd, GWL_STYLE);
    DWORD exstyle = (DWORD)GetWindowLongPtrA(g_hwnd, GWL_EXSTYLE);
    BOOL has_menu = GetMenu(g_hwnd) ? TRUE : FALSE;

    if(!style)
        style = WS_OVERLAPPEDWINDOW;

    AdjustWindowRectEx(&rc, style, has_menu, exstyle);
    SetWindowPos(g_hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void apply_startup_window_size(void)
{
    int scale, mode, smooth;
    (void)smooth;

    if(g_start_fullscreen)
        return;

    PCFX_Win32_GetScaleMode(&scale, &mode, &smooth);
    if(mode == PCFX_WIN32_SCALE_FIXED)
        resize_for_scale(scale);
}

static void set_scale(int scale)
{
    int old_scale, mode, smooth;
    (void)old_scale;
    PCFX_Win32_GetScaleMode(&old_scale, &mode, &smooth);

    /* The explicit 1x..6x menu is a window-size command, not a fullscreen
     * viewport command.  If it is used while fullscreen, first leave
     * fullscreen and restore the normal decorated window, then apply the
     * requested client size. */
    if(g_fullscreen)
        toggle_fullscreen(g_hwnd);

    mode = PCFX_WIN32_SCALE_FIXED;
    PCFX_Win32_SetScaleMode(scale, mode, smooth);
    resize_for_scale(scale);
    force_video_redraw(g_hwnd);
    update_menu_checks();
    save_config();
}

static void set_video_mode(int mode)
{
    int scale, old_mode, smooth;
    PCFX_Win32_GetScaleMode(&scale, &old_mode, &smooth);
    (void)old_mode;

    /* Fixed window size is inherently a windowed presentation mode.  Keeping a
     * borderless fullscreen window active while drawing a fixed 1x..6x
     * rectangle made later mode transitions inherit stale fullscreen geometry. */
    if(mode == PCFX_WIN32_SCALE_FIXED && g_fullscreen)
        toggle_fullscreen(g_hwnd);

    PCFX_Win32_SetScaleMode(scale, mode, smooth);
    if(mode == PCFX_WIN32_SCALE_FIXED)
        resize_for_scale(scale);
    force_video_redraw(g_hwnd);
    update_menu_checks();
    save_config();
}

static void toggle_smooth_stretch(void)
{
    int scale, mode, smooth;
    PCFX_Win32_GetScaleMode(&scale, &mode, &smooth);
    PCFX_Win32_SetScaleMode(scale, mode, !smooth);
    force_video_redraw(g_hwnd);
    update_menu_checks();
    save_config();
}

static void set_video_backend(HWND hwnd, int backend)
{
    PCFX_Win32_SetVideoBackend(backend);
    force_video_redraw(hwnd);
    update_menu_checks();
    save_config();
}

static void set_fullscreen_mode(HWND hwnd, int mode)
{
    if(g_fullscreen)
        audio_transition_mute_begin();
    PCFX_Win32_SetFullscreenMode(mode);
    PCFX_Win32_SetPresentationFullscreen(g_fullscreen);
    force_video_redraw(hwnd);
    UpdateWindow(hwnd);
    update_menu_checks();
    save_config();
    if(g_fullscreen)
        audio_transition_mute_end();
}

static void toggle_fullscreen(HWND hwnd)
{
    if(!hwnd)
        return;

    audio_transition_mute_begin();

    if(!g_fullscreen)
    {
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        g_window_placement.length = sizeof(g_window_placement);
        DWORD current_style = (DWORD)GetWindowLongPtrA(hwnd, GWL_STYLE);
        DWORD current_exstyle = (DWORD)GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
        g_window_style = sanitize_windowed_style(current_style);
        g_window_exstyle = sanitize_windowed_exstyle(current_style, current_exstyle);
        GetWindowPlacement(hwnd, &g_window_placement);
        if(g_window_placement.showCmd == SW_SHOWMAXIMIZED ||
           g_window_placement.showCmd == SW_SHOWMINIMIZED ||
           g_window_placement.showCmd == SW_HIDE)
            g_window_placement.showCmd = SW_SHOWNORMAL;
        if(!GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
            SystemParametersInfoA(SPI_GETWORKAREA, 0, &mi.rcMonitor, 0);

        SetMenu(hwnd, NULL);
        SetWindowLongPtrA(hwnd, GWL_STYLE, (LONG_PTR)(WS_POPUP | WS_VISIBLE));
        SetWindowLongPtrA(hwnd, GWL_EXSTYLE, 0);
        SetWindowPos(hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_fullscreen = 1;
        option.fullscreen = 1;
        PCFX_Win32_SetPresentationFullscreen(1);
        PCFX_Win32_SetClientSize(mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top);
    }
    else
    {
        /* Leave D3D11 exclusive fullscreen before restoring the decorated
         * window.  Some DXGI implementations keep the display mode locked if
         * the HWND style changes first. */
        PCFX_Win32_SetPresentationFullscreen(0);
        g_window_style = sanitize_windowed_style(g_window_style);
        g_window_exstyle = sanitize_windowed_exstyle(g_window_style, g_window_exstyle);
        SetWindowLongPtrA(hwnd, GWL_STYLE, (LONG_PTR)g_window_style);
        SetWindowLongPtrA(hwnd, GWL_EXSTYLE, (LONG_PTR)g_window_exstyle);
        SetMenu(hwnd, g_launchbox_mode ? NULL : (g_window_menu ? g_window_menu : g_menu));
        WINDOWPLACEMENT wp_restore = g_window_placement;
        wp_restore.length = sizeof(wp_restore);
        if(wp_restore.showCmd == SW_SHOWMAXIMIZED ||
           wp_restore.showCmd == SW_SHOWMINIMIZED ||
           wp_restore.showCmd == SW_HIDE)
            wp_restore.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(hwnd, &wp_restore);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        g_fullscreen = 0;
        option.fullscreen = 0;
        {
            RECT rc;
            if(GetClientRect(hwnd, &rc))
                PCFX_Win32_SetClientSize(rc.right - rc.left, rc.bottom - rc.top);
        }
    }

    update_menu_checks();
    DrawMenuBar(hwnd);
    force_video_redraw(hwnd);
    UpdateWindow(hwnd);
    save_config();
    audio_transition_mute_end();
}

static int show_audio_error(HWND hwnd, int backend)
{
    char msg[768];
    snprintf(msg, sizeof(msg),
#if defined(PCFX_WIN32_HAVE_WASAPI)
             "Could not open %s audio.\n\n%s\n\nSelect Audio > WaveOut if the WASAPI device or exclusive-mode format is unavailable.",
#else
             "Could not open %s audio.\n\n%s",
#endif
             PCFX_Win32_AudioBackendName(backend),
             PCFX_Win32_AudioLastError());
    pcfx_message_box(hwnd, msg, "PCFXEmu - Audio output failed", MB_ICONERROR | MB_OK);
    return 0;
}

static void set_audio_backend(HWND hwnd, int backend)
{
    if(backend == PCFX_Win32_AudioGetBackend())
        return;

    if(g_audio_open)
    {
        Audio_Close();
        g_audio_open = 0;
    }

    PCFX_Win32_AudioSetBackend(backend);

    if(g_game_loaded)
    {
        if(Audio_Init() == 0)
            g_audio_open = 1;
        else
            show_audio_error(hwnd, backend);
    }

    update_menu_checks();
    save_config();
}

#ifdef PCFX_ADPCM_COMPAT_OPTIONS
static void set_adpcm_buggy_mode(int mode)
{
    if(mode < PCFX_ADPCM_BUGGY_AUTO || mode > PCFX_ADPCM_BUGGY_ON)
        mode = PCFX_ADPCM_BUGGY_AUTO;
    g_adpcm_buggy_codec_mode = mode;
    PCFX_SetADPCMCompatOptions(g_adpcm_buggy_codec_mode,
                               g_adpcm_suppress_reset_clicks ? true : false);
    update_menu_checks();
    save_config();
}

static void toggle_adpcm_suppress_clicks(void)
{
    g_adpcm_suppress_reset_clicks = !g_adpcm_suppress_reset_clicks;
    PCFX_SetADPCMCompatOptions(g_adpcm_buggy_codec_mode,
                               g_adpcm_suppress_reset_clicks ? true : false);
    update_menu_checks();
    save_config();
}
#endif

static void set_cd_speed(HWND hwnd, int speed)
{
    (void)hwnd;
    if(speed < 1) speed = 1;
    if(speed > 16) speed = 16;
    g_cd_speed = speed;
    PCFX_SetCDSpeed((uint_fast32_t)g_cd_speed);
    update_menu_checks();
    save_config();
}


static void throttle_frame(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if(g_next_frame.QuadPart == 0)
        g_next_frame = now;

    const LONGLONG frame_ticks = (LONGLONG)((double)g_qpc_freq.QuadPart / 59.94);
    if(now.QuadPart < g_next_frame.QuadPart)
    {
        LONGLONG diff = g_next_frame.QuadPart - now.QuadPart;
        DWORD ms = (DWORD)((diff * 1000) / g_qpc_freq.QuadPart);
        if(ms > 1) Sleep(ms - 1);
        do { QueryPerformanceCounter(&now); } while(now.QuadPart < g_next_frame.QuadPart);
    }
    g_next_frame.QuadPart += frame_ticks;
    if(now.QuadPart - g_next_frame.QuadPart > frame_ticks * 4)
        g_next_frame = now;
}

static int path_is_huexe(const char* path)
{
    if(!path) return 0;
    const char* dot = strrchr(path_basename_win32(path), '.');
    if(!dot) return 0;
    return lstrcmpiA(dot, ".ex") == 0 || lstrcmpiA(dot, ".exe") == 0;
}

static int select_bios_root_for_request(const char* media_path, char* out, size_t out_size)
{
    const int huexe = path_is_huexe(media_path);
    if(g_system_mode == 1)
    {
        if(folder_has_bios_kind(g_exe_dir, 1)) { safe_copy(out, out_size, g_exe_dir); return 1; }
        if(g_common_doc_dir[0] && folder_has_bios_kind(g_common_doc_dir, 1)) { safe_copy(out, out_size, g_common_doc_dir); return 1; }
        safe_copy(out, out_size, g_exe_dir);
        return 0;
    }
    if(g_system_mode == 0)
    {
        if(folder_has_bios_kind(g_exe_dir, 0)) { safe_copy(out, out_size, g_exe_dir); return 1; }
        if(g_common_doc_dir[0] && folder_has_bios_kind(g_common_doc_dir, 0)) { safe_copy(out, out_size, g_common_doc_dir); return 1; }
        safe_copy(out, out_size, g_exe_dir);
        return 0;
    }

    /* Auto mode should not require the PC-FXGA BIOS for HuEXE / FXGA-flagged
     * software.  Prefer PC-FXGA for HuEXE if it exists, but fall back to the
     * normal PC-FX BIOS before failing. */
    if(huexe)
    {
        if(folder_has_bios_kind(g_exe_dir, 1)) { safe_copy(out, out_size, g_exe_dir); return 1; }
        if(g_common_doc_dir[0] && folder_has_bios_kind(g_common_doc_dir, 1)) { safe_copy(out, out_size, g_common_doc_dir); return 1; }
        if(folder_has_bios_kind(g_exe_dir, 0)) { safe_copy(out, out_size, g_exe_dir); return 1; }
        if(g_common_doc_dir[0] && folder_has_bios_kind(g_common_doc_dir, 0)) { safe_copy(out, out_size, g_common_doc_dir); return 1; }
        safe_copy(out, out_size, g_exe_dir);
        return 0;
    }

    return select_bios_root(out, out_size);
}

static int check_bios_or_message(HWND hwnd, const char* media_path)
{
    char root[2048];
    int ok = select_bios_root_for_request(media_path, root, sizeof(root));
    if(!ok)
    {
        show_missing_bios_message(hwnd);
        return 0;
    }
    safe_copy(home_path, sizeof(home_path), root);
    join_path(conf_path, sizeof(conf_path), home_path, "conf");
    join_path(save_path, sizeof(save_path), home_path, "sstates");
    join_path(sram_path, sizeof(sram_path), home_path, "sram");
    ensure_frontend_dirs();
    join_path(g_ini_path, sizeof(g_ini_path), conf_path, "pcfx_win32.ini");
    return 1;
}

static int open_audio_once(HWND hwnd)
{
    if(!g_audio_open)
    {
        update_audio_mute_state();
        if(Audio_Init() == 0)
            g_audio_open = 1;
        else
            show_audio_error(hwnd, PCFX_Win32_AudioGetBackend());
        update_audio_mute_state();
    }
    return g_audio_open;
}

static int load_game_path(HWND hwnd, const char* path)
{
    if(!path || !path[0])
        return 0;
    if(!check_bios_or_message(hwnd, path))
        return 0;

    apply_core_options();
    if(g_game_loaded)
    {
        sram_menu(0);
        PCFX_CoreClose();
        g_game_loaded = 0;
    }
    if(!g_core_initialized)
    {
        Emu_Init();
        g_core_initialized = 1;
    }
    else
    {
        Emu_Init();
    }

    set_game_name_from_path(path);
    if(!Load_Game_Memory((char*)path))
    {
        pcfx_message_box(hwnd, "The media could not be loaded. Check the image path or physical CD drive, BIOS mode, and BIOS file.", "PCFXEmu - Load failed", MB_ICONERROR | MB_OK);
        return 0;
    }
    Load_Configuration();
    g_game_loaded = 1;
    open_audio_once(hwnd);
    update_audio_mute_state();
    exit_vb = 0;
    SetWindowTextA(hwnd, CDIF_IsPhysicalPath_C(path) ? "PCFXEmu - Physical CD-ROM" : path_basename_win32(path));
    g_next_frame.QuadPart = 0;
    return 1;
}

static int boot_bios(HWND hwnd)
{
    if(!check_bios_or_message(hwnd, NULL))
        return 0;
    apply_core_options();
    if(g_game_loaded)
    {
        sram_menu(0);
        PCFX_CoreClose();
        g_game_loaded = 0;
    }
    if(!g_core_initialized)
    {
        Emu_Init();
        g_core_initialized = 1;
    }
    else
    {
        Emu_Init();
    }
    safe_copy(GameName_emu, sizeof(GameName_emu), "bios");
    if(!Load_BIOS_Memory())
    {
        pcfx_message_box(hwnd, "The BIOS could not be booted. Check the BIOS mode and BIOS image.", "PCFXEmu - BIOS boot failed", MB_ICONERROR | MB_OK);
        return 0;
    }
    Load_Configuration();
    g_game_loaded = 1;
    open_audio_once(hwnd);
    update_audio_mute_state();
    exit_vb = 0;
    SetWindowTextA(hwnd, "PCFXEmu - BIOS");
    g_next_frame.QuadPart = 0;
    return 1;
}

static void browse_load(HWND hwnd)
{
    char path[2048] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PC-FX / PC-FXGA images\0*.cue;*.ccd;*.toc;*.chd;*.m3u;*.bin;*.iso;*.img;*.ex;*.exe\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = "Load PC-FX / PC-FXGA image";
    g_open_hotkey_dialog_active = 1;
    audio_modal_mute_begin();
    int selected = GetOpenFileNameA(&ofn) ? 1 : 0;
    audio_modal_mute_end();
    g_open_hotkey_dialog_active = 0;
    g_open_hotkey_pad_prev = 1;
    if(selected)
        load_game_path(hwnd, path);
}

#ifdef PCFX_ENABLE_PHYSICAL_CD
static void load_physical_cd(HWND hwnd)
{
    load_game_path(hwnd, "cdrom:");
}
#endif

static void close_game(void)
{
    if(g_game_loaded)
    {
        sram_menu(0);
        PCFX_CoreClose();
        if(g_audio_open)
        {
            Audio_Close();
            g_audio_open = 0;
        }
        g_game_loaded = 0;
        update_audio_mute_state();
        GameName_emu[0] = 0;
        SetWindowTextA(g_hwnd, "PCFXEmu");
        Clear_Video();
    }
}

static void show_about(HWND hwnd)
{
    char msg[1024];
    snprintf(msg, sizeof(msg),
             "PCFXEmu\n"
             "Author: gameblabla\n"
             "Win32 frontend/backend additions in this build.\n\n"
             "Video backend: %s\n"
             "Fullscreen mode: %s\n"
             "Compile-time pixel format: %s\n"
             "Audio backend: %s\n"
             "Input: two-player keyboard/XInput remapping\n"
             "LaunchBox mode: %s\n\n"
             "BIOS search paths:\n%s\n%s",
             PCFX_Win32_VideoBackendName(PCFX_Win32_GetVideoBackend()),
             PCFX_Win32_GetFullscreenMode() == PCFX_WIN32_FULLSCREEN_BORDERLESS ? "borderless" : "exclusive",
             PCFX_Win32_GetPixelFormatName(),
             PCFX_Win32_AudioBackendName(PCFX_Win32_AudioGetBackend()),
             g_launchbox_mode ? "on" : "off",
             g_exe_dir[0] ? g_exe_dir : ".",
             g_common_doc_dir[0] ? g_common_doc_dir : "");
    pcfx_message_box(hwnd, msg, "About PCFXEmu", MB_ICONINFORMATION | MB_OK);
}

typedef struct MapDialogState
{
    HWND hwnd;
    HWND parent;
    HWND key_buttons[PCFX_WIN32_PLAYERS][PCFX_WIN32_BUTTONS];
    HWND xinput_buttons[PCFX_WIN32_PLAYERS][PCFX_WIN32_BUTTONS];
    uint32_t key_backup[PCFX_WIN32_PLAYERS][PCFX_WIN32_BUTTONS];
    uint32_t xinput_backup[PCFX_WIN32_PLAYERS][PCFX_WIN32_BUTTONS];
    int done;
    int cancelled;
    int capture_player;
    int capture_button;
    int capture_kind; /* 0=keyboard, 1=XInput */
} MapDialogState;

#define ID_MAP_TIMER_XINPUT 1

static void map_update_key_button(MapDialogState* st, int p, int b)
{
    char vkname[64];
    PCFX_Win32_InputVKName(PCFX_Win32_InputGetMapping(p, b), vkname, sizeof(vkname));
    SetWindowTextA(st->key_buttons[p][b], vkname);
}

static void map_update_xinput_button(MapDialogState* st, int p, int b)
{
    char name[64];
    PCFX_Win32_InputXInputName(PCFX_Win32_InputGetXInputMapping(p, b), name, sizeof(name));
    SetWindowTextA(st->xinput_buttons[p][b], name);
}

static void map_update_button(MapDialogState* st, int p, int b)
{
    map_update_key_button(st, p, b);
    map_update_xinput_button(st, p, b);
}

static void map_update_all(MapDialogState* st)
{
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
        for(int b = 0; b < PCFX_WIN32_BUTTONS; b++)
            map_update_button(st, p, b);
}

static void map_begin_capture(MapDialogState* st, int kind, int p, int b)
{
    st->capture_kind = kind;
    st->capture_player = p;
    st->capture_button = b;
    if(kind)
    {
        SetWindowTextA(st->xinput_buttons[p][b], "Press pad input...");
        SetTimer(st->hwnd, ID_MAP_TIMER_XINPUT, 25, NULL);
    }
    else
    {
        SetWindowTextA(st->key_buttons[p][b], "Press a key...");
        KillTimer(st->hwnd, ID_MAP_TIMER_XINPUT);
    }
    SetFocus(st->hwnd);
}

static void map_cancel_capture(MapDialogState* st)
{
    if(st->capture_player >= 0)
        map_update_button(st, st->capture_player, st->capture_button);
    KillTimer(st->hwnd, ID_MAP_TIMER_XINPUT);
    st->capture_player = -1;
    st->capture_button = -1;
    st->capture_kind = 0;
}

static LRESULT CALLBACK MapWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MapDialogState* st = (MapDialogState*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch(msg)
    {
        case WM_NCCREATE:
        {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lp;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }
        case WM_CREATE:
        {
            st = (MapDialogState*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
            st->hwnd = hwnd;
            st->capture_player = -1;
            st->capture_button = -1;
            st->capture_kind = 0;
            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            CreateWindowA("STATIC",
                          "Click a keyboard or XInput field, then press the replacement input. Delete clears a field. Escape cancels capture.",
                          WS_CHILD | WS_VISIBLE, 12, 10, 900, 20, hwnd, NULL, g_hinst, NULL);
            char padnote[160];
            snprintf(padnote, sizeof(padnote), "XInput: Player 1 uses controller %d; Player 2 uses controller %d. Toggle XInput from the Input menu.",
                     PCFX_Win32_InputGetXInputUser(0) + 1, PCFX_Win32_InputGetXInputUser(1) + 1);
            CreateWindowA("STATIC", padnote, WS_CHILD | WS_VISIBLE, 12, 31, 900, 20, hwnd, NULL, g_hinst, NULL);
            CreateWindowA("STATIC", "Player 1 Keyboard", WS_CHILD | WS_VISIBLE, 135, 58, 140, 20, hwnd, NULL, g_hinst, NULL);
            CreateWindowA("STATIC", "Player 1 XInput",  WS_CHILD | WS_VISIBLE, 310, 58, 140, 20, hwnd, NULL, g_hinst, NULL);
            CreateWindowA("STATIC", "Player 2 Keyboard", WS_CHILD | WS_VISIBLE, 515, 58, 140, 20, hwnd, NULL, g_hinst, NULL);
            CreateWindowA("STATIC", "Player 2 XInput",  WS_CHILD | WS_VISIBLE, 690, 58, 140, 20, hwnd, NULL, g_hinst, NULL);
            for(int b = 0; b < PCFX_WIN32_BUTTONS; b++)
            {
                int y = 84 + b * 25;
                HWND label = CreateWindowA("STATIC", PCFX_Win32_InputButtonName(b), WS_CHILD | WS_VISIBLE,
                                           12, y + 4, 110, 20, hwnd, NULL, g_hinst, NULL);
                SendMessage(label, WM_SETFONT, (WPARAM)font, TRUE);
                for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
                {
                    int key_x = p ? 515 : 135;
                    int xi_x  = p ? 690 : 310;
                    st->key_buttons[p][b] = CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                                          key_x, y, 160, 23, hwnd,
                                                          (HMENU)(UINT_PTR)(ID_MAP_BASE + p * PCFX_WIN32_BUTTONS + b),
                                                          g_hinst, NULL);
                    st->xinput_buttons[p][b] = CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                                             xi_x, y, 160, 23, hwnd,
                                                             (HMENU)(UINT_PTR)(ID_MAP_BASE + 100 + p * PCFX_WIN32_BUTTONS + b),
                                                             g_hinst, NULL);
                    SendMessage(st->key_buttons[p][b], WM_SETFONT, (WPARAM)font, TRUE);
                    SendMessage(st->xinput_buttons[p][b], WM_SETFONT, (WPARAM)font, TRUE);
                }
            }
            HWND def = CreateWindowA("BUTTON", "Defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     405, 446, 90, 26, hwnd, (HMENU)(UINT_PTR)ID_MAP_DEFAULTS, g_hinst, NULL);
            HWND ok = CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                    505, 446, 90, 26, hwnd, (HMENU)(UINT_PTR)ID_MAP_OK, g_hinst, NULL);
            HWND cancel = CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                        605, 446, 90, 26, hwnd, (HMENU)(UINT_PTR)ID_MAP_CANCEL, g_hinst, NULL);
            SendMessage(def, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(ok, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(cancel, WM_SETFONT, (WPARAM)font, TRUE);
            map_update_all(st);
            return 0;
        }
        case WM_TIMER:
            if(st && wp == ID_MAP_TIMER_XINPUT && st->capture_player >= 0 && st->capture_kind == 1)
            {
                uint32_t code = 0;
                if(PCFX_Win32_InputPollXInputCapture(st->capture_player, &code))
                {
                    PCFX_Win32_InputSetXInputMapping(st->capture_player, st->capture_button, code);
                    map_cancel_capture(st);
                }
                return 0;
            }
            break;
        case WM_COMMAND:
        {
            int id = LOWORD(wp);
            if(id >= ID_MAP_BASE && id < ID_MAP_BASE + PCFX_WIN32_PLAYERS * PCFX_WIN32_BUTTONS)
            {
                int n = id - ID_MAP_BASE;
                map_begin_capture(st, 0, n / PCFX_WIN32_BUTTONS, n % PCFX_WIN32_BUTTONS);
                return 0;
            }
            if(id >= ID_MAP_BASE + 100 && id < ID_MAP_BASE + 100 + PCFX_WIN32_PLAYERS * PCFX_WIN32_BUTTONS)
            {
                int n = id - (ID_MAP_BASE + 100);
                map_begin_capture(st, 1, n / PCFX_WIN32_BUTTONS, n % PCFX_WIN32_BUTTONS);
                return 0;
            }
            if(id == ID_MAP_DEFAULTS)
            {
                PCFX_Win32_InputDefaults();
                map_update_all(st);
                return 0;
            }
            if(id == ID_MAP_OK)
            {
                st->done = 1;
                DestroyWindow(hwnd);
                return 0;
            }
            if(id == ID_MAP_CANCEL)
            {
                st->cancelled = 1;
                st->done = 1;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
            if(st && st->capture_player >= 0)
            {
                if(wp == VK_ESCAPE)
                {
                    map_cancel_capture(st);
                    return 0;
                }
                if(st->capture_kind == 0)
                {
                    if(wp == VK_DELETE || wp == VK_BACK)
                        PCFX_Win32_InputSetMapping(st->capture_player, st->capture_button, 0);
                    else
                        PCFX_Win32_InputSetMapping(st->capture_player, st->capture_button, (uint32_t)wp);
                    map_cancel_capture(st);
                    return 0;
                }
                if(st->capture_kind == 1 && (wp == VK_DELETE || wp == VK_BACK))
                {
                    PCFX_Win32_InputSetXInputMapping(st->capture_player, st->capture_button, 0);
                    map_cancel_capture(st);
                    return 0;
                }
                return 0;
            }
            if(st && wp == VK_ESCAPE)
            {
                st->cancelled = 1;
                st->done = 1;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            if(st)
            {
                st->cancelled = 1;
                st->done = 1;
            }
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void show_input_dialog(HWND parent)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = MapWndProc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = MAP_CLASS_NAME;
    RegisterClassA(&wc);

    MapDialogState st;
    memset(&st, 0, sizeof(st));
    st.parent = parent;
    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
        for(int b = 0; b < PCFX_WIN32_BUTTONS; b++)
        {
            st.key_backup[p][b] = PCFX_Win32_InputGetMapping(p, b);
            st.xinput_backup[p][b] = PCFX_Win32_InputGetXInputMapping(p, b);
        }

    RECT rc = {0, 0, 875, 520};
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE);
    HWND dlg = CreateWindowExA(WS_EX_DLGMODALFRAME, MAP_CLASS_NAME, "Configure Controllers",
                               WS_CAPTION | WS_SYSMENU | WS_POPUP,
                               CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                               parent, NULL, g_hinst, &st);
    if(!dlg)
        return;
    audio_modal_mute_begin();
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg;
    while(!st.done && GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        if(!IsDialogMessageA(dlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    audio_modal_mute_end();
    if(st.cancelled)
    {
        for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
            for(int b = 0; b < PCFX_WIN32_BUTTONS; b++)
            {
                PCFX_Win32_InputSetMapping(p, b, st.key_backup[p][b]);
                PCFX_Win32_InputSetXInputMapping(p, b, st.xinput_backup[p][b]);
            }
    }
    else
    {
        save_config();
    }
}


typedef struct HotkeyDialogState
{
    HWND hwnd;
    HWND parent;
    HWND key_button;
    HWND pad_button;
    HWND pad_player_combo;
    uint32_t key_backup;
    int pad_player_backup;
    uint32_t pad_code_backup;
    int done;
    int cancelled;
    int capture_kind; /* 0=none, 1=keyboard, 2=XInput */
} HotkeyDialogState;

static void hotkey_update_key_button(HotkeyDialogState* st)
{
    char name[64];
    PCFX_Win32_InputVKName(g_open_hotkey_key, name, sizeof(name));
    SetWindowTextA(st->key_button, name);
}

static void hotkey_update_pad_button(HotkeyDialogState* st)
{
    char name[64];
    PCFX_Win32_InputXInputName(g_open_hotkey_xinput_code, name, sizeof(name));
    SetWindowTextA(st->pad_button, name);
}

static void hotkey_cancel_capture(HotkeyDialogState* st)
{
    KillTimer(st->hwnd, ID_HOTKEY_TIMER_XINPUT);
    st->capture_kind = 0;
    hotkey_update_key_button(st);
    hotkey_update_pad_button(st);
}

static LRESULT CALLBACK HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    HotkeyDialogState* st = (HotkeyDialogState*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch(msg)
    {
        case WM_NCCREATE:
        {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lp;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }
        case WM_CREATE:
        {
            st = (HotkeyDialogState*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
            st->hwnd = hwnd;
            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            CreateWindowA("STATIC",
                          "These hotkeys are frontend-only. They do not change PC-FX pad mappings.",
                          WS_CHILD | WS_VISIBLE, 12, 12, 520, 20, hwnd, NULL, g_hinst, NULL);
            CreateWindowA("STATIC", "Open new game - keyboard:", WS_CHILD | WS_VISIBLE,
                          12, 48, 180, 20, hwnd, NULL, g_hinst, NULL);
            st->key_button = CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                           210, 44, 170, 24, hwnd, (HMENU)(UINT_PTR)ID_HOTKEY_KEY_OPEN, g_hinst, NULL);
            CreateWindowA("STATIC", "Open new game - gamepad:", WS_CHILD | WS_VISIBLE,
                          12, 82, 180, 20, hwnd, NULL, g_hinst, NULL);
            st->pad_button = CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                           210, 78, 170, 24, hwnd, (HMENU)(UINT_PTR)ID_HOTKEY_PAD_OPEN, g_hinst, NULL);
            CreateWindowA("STATIC", "Gamepad source:", WS_CHILD | WS_VISIBLE,
                          12, 116, 180, 20, hwnd, NULL, g_hinst, NULL);
            st->pad_player_combo = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                                 210, 112, 170, 120, hwnd, (HMENU)(UINT_PTR)ID_HOTKEY_PAD_PLAYER, g_hinst, NULL);
            SendMessageA(st->pad_player_combo, CB_ADDSTRING, 0, (LPARAM)"Player 1 XInput");
            SendMessageA(st->pad_player_combo, CB_ADDSTRING, 0, (LPARAM)"Player 2 XInput");
            SendMessageA(st->pad_player_combo, CB_SETCURSEL, (WPARAM)g_open_hotkey_xinput_player, 0);

            HWND def = CreateWindowA("BUTTON", "Defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     110, 158, 90, 26, hwnd, (HMENU)(UINT_PTR)ID_HOTKEY_DEFAULTS, g_hinst, NULL);
            HWND ok = CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                    210, 158, 90, 26, hwnd, (HMENU)(UINT_PTR)ID_HOTKEY_OK, g_hinst, NULL);
            HWND cancel = CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                        310, 158, 90, 26, hwnd, (HMENU)(UINT_PTR)ID_HOTKEY_CANCEL, g_hinst, NULL);
            SendMessage(st->key_button, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(st->pad_button, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(st->pad_player_combo, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(def, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(ok, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessage(cancel, WM_SETFONT, (WPARAM)font, TRUE);
            hotkey_update_key_button(st);
            hotkey_update_pad_button(st);
            return 0;
        }
        case WM_TIMER:
            if(st && wp == ID_HOTKEY_TIMER_XINPUT && st->capture_kind == 2)
            {
                uint32_t code = 0;
                int player = (int)SendMessageA(st->pad_player_combo, CB_GETCURSEL, 0, 0);
                if(player < 0) player = 0;
                if(PCFX_Win32_InputPollXInputCapture(player, &code))
                {
                    g_open_hotkey_xinput_player = player;
                    g_open_hotkey_xinput_code = code;
                    hotkey_cancel_capture(st);
                }
                return 0;
            }
            break;
        case WM_COMMAND:
        {
            int id = LOWORD(wp);
            if(id == ID_HOTKEY_KEY_OPEN)
            {
                st->capture_kind = 1;
                KillTimer(hwnd, ID_HOTKEY_TIMER_XINPUT);
                SetWindowTextA(st->key_button, "Press a key...");
                SetFocus(hwnd);
                return 0;
            }
            if(id == ID_HOTKEY_PAD_OPEN)
            {
                st->capture_kind = 2;
                SetWindowTextA(st->pad_button, "Press pad input...");
                SetTimer(hwnd, ID_HOTKEY_TIMER_XINPUT, 25, NULL);
                SetFocus(hwnd);
                return 0;
            }
            if(id == ID_HOTKEY_DEFAULTS)
            {
                g_open_hotkey_key = VK_F10;
                g_open_hotkey_xinput_player = 0;
                g_open_hotkey_xinput_code = XINPUT_GAMEPAD_RIGHT_THUMB;
                SendMessageA(st->pad_player_combo, CB_SETCURSEL, 0, 0);
                hotkey_cancel_capture(st);
                return 0;
            }
            if(id == ID_HOTKEY_OK)
            {
                int player = (int)SendMessageA(st->pad_player_combo, CB_GETCURSEL, 0, 0);
                if(player >= 0 && player < PCFX_WIN32_PLAYERS)
                    g_open_hotkey_xinput_player = player;
                st->done = 1;
                DestroyWindow(hwnd);
                return 0;
            }
            if(id == ID_HOTKEY_CANCEL)
            {
                st->cancelled = 1;
                st->done = 1;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
            if(st && st->capture_kind == 1)
            {
                if(wp == VK_ESCAPE)
                {
                    hotkey_cancel_capture(st);
                    return 0;
                }
                if(wp == VK_DELETE || wp == VK_BACK)
                    g_open_hotkey_key = 0;
                else
                    g_open_hotkey_key = (uint32_t)wp;
                hotkey_cancel_capture(st);
                return 0;
            }
            if(st && st->capture_kind == 2 && (wp == VK_DELETE || wp == VK_BACK))
            {
                g_open_hotkey_xinput_code = 0;
                hotkey_cancel_capture(st);
                return 0;
            }
            if(st && wp == VK_ESCAPE)
            {
                st->cancelled = 1;
                st->done = 1;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            if(st)
            {
                st->cancelled = 1;
                st->done = 1;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, ID_HOTKEY_TIMER_XINPUT);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void show_hotkey_dialog(HWND parent)
{
    static int registered;
    if(!registered)
    {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = HotkeyWndProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "PCFXEmuWin32HotkeyDialog";
        RegisterClassA(&wc);
        registered = 1;
    }

    HotkeyDialogState st;
    memset(&st, 0, sizeof(st));
    st.parent = parent;
    st.key_backup = g_open_hotkey_key;
    st.pad_player_backup = g_open_hotkey_xinput_player;
    st.pad_code_backup = g_open_hotkey_xinput_code;

    RECT rc = {0, 0, 430, 230};
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE);
    HWND dlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "PCFXEmuWin32HotkeyDialog", "Configure Hotkeys",
                               WS_CAPTION | WS_SYSMENU | WS_POPUP,
                               CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                               parent, NULL, g_hinst, &st);
    if(!dlg)
        return;

    g_open_hotkey_dialog_active = 1;
    audio_modal_mute_begin();
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg;
    while(!st.done && GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        if(!IsDialogMessageA(dlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    g_open_hotkey_dialog_active = 0;
    audio_modal_mute_end();

    if(st.cancelled)
    {
        g_open_hotkey_key = st.key_backup;
        g_open_hotkey_xinput_player = st.pad_player_backup;
        g_open_hotkey_xinput_code = st.pad_code_backup;
    }
    else
    {
        save_config();
    }
}

static void handle_command(HWND hwnd, int id)
{
    switch(id)
    {
        case ID_FILE_LOAD: browse_load(hwnd); break;
#ifdef PCFX_ENABLE_PHYSICAL_CD
        case ID_FILE_LOAD_PHYSICAL: load_physical_cd(hwnd); break;
#endif
        case ID_FILE_BOOT_BIOS: boot_bios(hwnd); break;
        case ID_FILE_CLOSE: close_game(); break;
        case ID_FILE_EXIT: PostMessageA(hwnd, WM_CLOSE, 0, 0); break;
        case ID_SYSTEM_RESET:
            if(g_game_loaded) PCFX_SoftReset();
            break;
        case ID_SYSTEM_MODE_PCFX:
        case ID_SYSTEM_MODE_FXGA:
        case ID_SYSTEM_MODE_AUTO:
            g_system_mode = id == ID_SYSTEM_MODE_PCFX ? 0 : id == ID_SYSTEM_MODE_FXGA ? 1 : 2;
            apply_core_options();
            update_menu_checks();
            save_config();
            break;
        case ID_SYSTEM_HUC6273:
            g_huc6273_enabled = !g_huc6273_enabled;
            apply_core_options();
            update_menu_checks();
            save_config();
            break;
        case ID_SYSTEM_PATCH_SHORTINTRO:
            toggle_bios_patch(hwnd, PCFX_BIOS_PATCH_SHORTINTRO);
            break;
        case ID_SYSTEM_PATCH_ENGLISH:
            toggle_bios_patch(hwnd, PCFX_BIOS_PATCH_ENGLISH);
            break;
        case ID_SYSTEM_PATCH_AUTOLAUNCH:
            toggle_bios_patch(hwnd, PCFX_BIOS_PATCH_AUTOLAUNCH);
            break;
        case ID_STATE_LOAD:
            state_menu_action(hwnd, 1);
            break;
        case ID_STATE_SAVE:
            state_menu_action(hwnd, 0);
            break;
        case ID_STATE_SLOT_PREV:
            change_save_slot(hwnd, -1);
            break;
        case ID_STATE_SLOT_NEXT:
            change_save_slot(hwnd, 1);
            break;
        case ID_STATE_SLOT_0:
        case ID_STATE_SLOT_1:
        case ID_STATE_SLOT_2:
        case ID_STATE_SLOT_3:
        case ID_STATE_SLOT_4:
        case ID_STATE_SLOT_5:
        case ID_STATE_SLOT_6:
        case ID_STATE_SLOT_7:
        case ID_STATE_SLOT_8:
        case ID_STATE_SLOT_9:
            set_save_slot(hwnd, id - ID_STATE_SLOT_0);
            break;
        case ID_VIDEO_SCALE_1X: set_scale(1); break;
        case ID_VIDEO_SCALE_2X: set_scale(2); break;
        case ID_VIDEO_SCALE_3X: set_scale(3); break;
        case ID_VIDEO_SCALE_4X: set_scale(4); break;
        case ID_VIDEO_SCALE_5X: set_scale(5); break;
        case ID_VIDEO_SCALE_6X: set_scale(6); break;
        case ID_VIDEO_MODE_FIXED:
            set_video_mode(PCFX_WIN32_SCALE_FIXED);
            break;
        case ID_VIDEO_STRETCH:
            set_video_mode(PCFX_WIN32_SCALE_STRETCH);
            break;
        case ID_VIDEO_KEEP_ASPECT:
            set_video_mode(PCFX_WIN32_SCALE_ASPECT);
            break;
        case ID_VIDEO_INTEGER:
            set_video_mode(PCFX_WIN32_SCALE_INTEGER);
            break;
        case ID_VIDEO_SMOOTH:
            toggle_smooth_stretch();
            break;
#if defined(PCFX_WIN32_HAVE_D3D11)
        case ID_VIDEO_BACKEND_D3D11:
            set_video_backend(hwnd, PCFX_WIN32_VIDEO_D3D11);
            break;
        case ID_VIDEO_BACKEND_GDI:
            set_video_backend(hwnd, PCFX_WIN32_VIDEO_GDI);
            break;
        case ID_VIDEO_FS_EXCLUSIVE:
            set_fullscreen_mode(hwnd, PCFX_WIN32_FULLSCREEN_EXCLUSIVE);
            break;
        case ID_VIDEO_FS_BORDERLESS:
            set_fullscreen_mode(hwnd, PCFX_WIN32_FULLSCREEN_BORDERLESS);
            break;
#endif
        case ID_VIDEO_FULLSCREEN:
            toggle_fullscreen(hwnd);
            break;
        case ID_AUDIO_WAVEOUT:
            set_audio_backend(hwnd, PCFX_WIN32_AUDIO_WAVEOUT);
            break;
#if defined(PCFX_WIN32_HAVE_WASAPI)
        case ID_AUDIO_WASAPI_SHARED:
            set_audio_backend(hwnd, PCFX_WIN32_AUDIO_WASAPI_SHARED);
            break;
        case ID_AUDIO_WASAPI_EXCLUSIVE:
            set_audio_backend(hwnd, PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE);
            break;
#endif
#ifdef PCFX_ADPCM_COMPAT_OPTIONS
        case ID_AUDIO_ADPCM_BUGGY_AUTO:
            set_adpcm_buggy_mode(PCFX_ADPCM_BUGGY_AUTO);
            break;
        case ID_AUDIO_ADPCM_BUGGY_OFF:
            set_adpcm_buggy_mode(PCFX_ADPCM_BUGGY_OFF);
            break;
        case ID_AUDIO_ADPCM_BUGGY_ON:
            set_adpcm_buggy_mode(PCFX_ADPCM_BUGGY_ON);
            break;
        case ID_AUDIO_ADPCM_SUPPRESS_CLICKS:
            toggle_adpcm_suppress_clicks();
            break;
#endif
        case ID_AUDIO_CD_SPEED_1X: set_cd_speed(hwnd, 1); break;
        case ID_AUDIO_CD_SPEED_2X: set_cd_speed(hwnd, 2); break;
        case ID_AUDIO_CD_SPEED_4X: set_cd_speed(hwnd, 4); break;
        case ID_AUDIO_CD_SPEED_8X: set_cd_speed(hwnd, 8); break;
        case ID_AUDIO_CD_SPEED_16X: set_cd_speed(hwnd, 16); break;
        case ID_INPUT_CONFIGURE:
            show_input_dialog(hwnd);
            break;
        case ID_INPUT_HOTKEYS:
            show_hotkey_dialog(hwnd);
            break;
        case ID_INPUT_XINPUT_ENABLED:
            PCFX_Win32_InputSetXInputEnabled(!PCFX_Win32_InputGetXInputEnabled());
            update_menu_checks();
            save_config();
            break;
        case ID_INPUT_PORT1_PAD:
            option.type_controller = 0;
            update_menu_checks();
            save_config();
            break;
        case ID_INPUT_PORT1_MOUSE:
            option.type_controller = 1;
            PCFX_Win32_InputResetMouse();
            update_menu_checks();
            save_config();
            break;
        case ID_HELP_ABOUT:
            show_about(hwnd);
            break;
    }
}


typedef struct CommandLineOptions
{
    int fullscreen_set;
    int fullscreen;
    int launchbox;
    int boot_bios;
    int show_help;
    int system_mode_set;
    int system_mode;
    int audio_backend_set;
    int audio_backend;
    int video_backend_set;
    int video_backend;
    int fullscreen_mode_set;
    int fullscreen_mode;
    int xinput_set;
    int xinput_enabled;
    char load_path[2048];
} CommandLineOptions;

static int option_match(const char* arg, const char* name)
{
    if(!arg || !name)
        return 0;
    if(arg[0] == '-' && arg[1] == '-')
        arg += 2;
    else if(arg[0] == '-' || arg[0] == '/')
        arg += 1;
    else
        return 0;
    return lstrcmpiA(arg, name) == 0;
}

static const char* option_value(const char* arg, const char* name)
{
    size_t n;
    if(!arg || !name)
        return NULL;
    if(arg[0] == '-' && arg[1] == '-')
        arg += 2;
    else if(arg[0] == '-' || arg[0] == '/')
        arg += 1;
    else
        return NULL;
    n = strlen(name);
    if(_strnicmp(arg, name, n) == 0 && arg[n] == '=')
        return arg + n + 1;
    return NULL;
}

static int wide_to_ansi_arg(const wchar_t* in, char* out, size_t out_size)
{
    if(!out || !out_size)
        return 0;
    out[0] = 0;
    if(!in)
        return 0;
    return WideCharToMultiByte(CP_ACP, 0, in, -1, out, (int)out_size, NULL, NULL) > 0;
}

static void parse_command_line_options(CommandLineOptions* opt)
{
    int argc = 0;
    LPWSTR* argvw;
    int positional = 0;
    memset(opt, 0, sizeof(*opt));

    argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
    if(!argvw)
        return;

    for(int i = 1; i < argc; i++)
    {
        char arg[2048];
        const char* value;
        if(!wide_to_ansi_arg(argvw[i], arg, sizeof(arg)))
            continue;

        if(option_match(arg, "fullscreen") || option_match(arg, "fs"))
        {
            opt->fullscreen_set = 1;
            opt->fullscreen = 1;
        }
        else if(option_match(arg, "windowed"))
        {
            opt->fullscreen_set = 1;
            opt->fullscreen = 0;
        }
        else if(option_match(arg, "launchbox"))
        {
            opt->launchbox = 1;
        }
        else if(option_match(arg, "bios"))
        {
            opt->boot_bios = 1;
        }
        else if(option_match(arg, "pcfx"))
        {
            opt->system_mode_set = 1;
            opt->system_mode = 0;
        }
        else if(option_match(arg, "pcfxga") || option_match(arg, "fxga"))
        {
            opt->system_mode_set = 1;
            opt->system_mode = 1;
        }
        else if(option_match(arg, "auto"))
        {
            opt->system_mode_set = 1;
            opt->system_mode = 2;
        }
        else if(option_match(arg, "xinput"))
        {
            opt->xinput_set = 1;
            opt->xinput_enabled = 1;
        }
        else if(option_match(arg, "no-xinput"))
        {
            opt->xinput_set = 1;
            opt->xinput_enabled = 0;
        }
        else if(option_match(arg, "help") || option_match(arg, "h") || option_match(arg, "?"))
        {
            opt->show_help = 1;
        }
        else if((value = option_value(arg, "audio")) != NULL)
        {
            if(lstrcmpiA(value, "waveout") == 0)
            {
                opt->audio_backend_set = 1;
                opt->audio_backend = PCFX_WIN32_AUDIO_WAVEOUT;
            }
#if defined(PCFX_WIN32_HAVE_WASAPI)
            else if(lstrcmpiA(value, "wasapi") == 0 || lstrcmpiA(value, "wasapi-shared") == 0)
            {
                opt->audio_backend_set = 1;
                opt->audio_backend = PCFX_WIN32_AUDIO_WASAPI_SHARED;
            }
            else if(lstrcmpiA(value, "wasapi-exclusive") == 0 || lstrcmpiA(value, "exclusive") == 0)
            {
                opt->audio_backend_set = 1;
                opt->audio_backend = PCFX_WIN32_AUDIO_WASAPI_EXCLUSIVE;
            }
#endif
        }
        else if((value = option_value(arg, "video")) != NULL)
        {
#if defined(PCFX_WIN32_HAVE_D3D11)
            if(lstrcmpiA(value, "d3d11") == 0 || lstrcmpiA(value, "d3d") == 0)
            {
                opt->video_backend_set = 1;
                opt->video_backend = PCFX_WIN32_VIDEO_D3D11;
            }
            else
#endif
            if(lstrcmpiA(value, "gdi") == 0)
            {
                opt->video_backend_set = 1;
                opt->video_backend = PCFX_WIN32_VIDEO_GDI;
            }
        }
        else if((value = option_value(arg, "fullscreen-mode")) != NULL)
        {
            if(lstrcmpiA(value, "exclusive") == 0 || lstrcmpiA(value, "real") == 0)
            {
                opt->fullscreen_mode_set = 1;
                opt->fullscreen_mode = PCFX_WIN32_FULLSCREEN_EXCLUSIVE;
            }
            else if(lstrcmpiA(value, "borderless") == 0 || lstrcmpiA(value, "desktop") == 0)
            {
                opt->fullscreen_mode_set = 1;
                opt->fullscreen_mode = PCFX_WIN32_FULLSCREEN_BORDERLESS;
            }
        }
        else if(option_match(arg, "exclusive-fullscreen"))
        {
            opt->fullscreen_mode_set = 1;
            opt->fullscreen_mode = PCFX_WIN32_FULLSCREEN_EXCLUSIVE;
        }
        else if(option_match(arg, "borderless-fullscreen"))
        {
            opt->fullscreen_mode_set = 1;
            opt->fullscreen_mode = PCFX_WIN32_FULLSCREEN_BORDERLESS;
        }
#ifdef PCFX_ENABLE_PHYSICAL_CD
        else if((value = option_value(arg, "physical-cd")) != NULL ||
                (value = option_value(arg, "cdrom")) != NULL)
        {
            snprintf(opt->load_path, sizeof(opt->load_path), "cdrom:%s", value);
            positional = 1;
        }
        else if(option_match(arg, "physical-cd") || option_match(arg, "cdrom"))
        {
            if(i + 1 < argc)
            {
                char next_arg[2048];
                if(wide_to_ansi_arg(argvw[i + 1], next_arg, sizeof(next_arg)) && next_arg[0] != '-' && next_arg[0] != '/')
                {
                    snprintf(opt->load_path, sizeof(opt->load_path), "cdrom:%s", next_arg);
                    i++;
                }
                else
                    safe_copy(opt->load_path, sizeof(opt->load_path), "cdrom:");
            }
            else
                safe_copy(opt->load_path, sizeof(opt->load_path), "cdrom:");
            positional = 1;
        }
#endif
        else if((value = option_value(arg, "load")) != NULL)
        {
            safe_copy(opt->load_path, sizeof(opt->load_path), value);
            positional = 1;
        }
        else if(option_match(arg, "load") && i + 1 < argc)
        {
            wide_to_ansi_arg(argvw[++i], opt->load_path, sizeof(opt->load_path));
            positional = 1;
        }
        else if(strcmp(arg, "--") == 0 && i + 1 < argc)
        {
            wide_to_ansi_arg(argvw[++i], opt->load_path, sizeof(opt->load_path));
            positional = 1;
            break;
        }
        else if(!positional)
        {
            safe_copy(opt->load_path, sizeof(opt->load_path), arg);
            positional = 1;
        }
    }

    LocalFree(argvw);
}

static void apply_command_line_options(const CommandLineOptions* opt)
{
    if(!opt)
        return;
    g_launchbox_mode = opt->launchbox ? 1 : 0;
    if(opt->system_mode_set)
        g_system_mode = opt->system_mode;
    if(opt->audio_backend_set)
        PCFX_Win32_AudioSetBackend(opt->audio_backend);
    if(opt->video_backend_set)
        PCFX_Win32_SetVideoBackend(opt->video_backend);
    if(opt->fullscreen_mode_set)
        PCFX_Win32_SetFullscreenMode(opt->fullscreen_mode);
    if(opt->xinput_set)
        PCFX_Win32_InputSetXInputEnabled(opt->xinput_enabled);
    if(g_launchbox_mode && !opt->fullscreen_set)
        g_start_fullscreen = 1;
    if(opt->fullscreen_set)
        g_start_fullscreen = opt->fullscreen ? 1 : 0;
}

static void show_command_line_help(HWND hwnd)
{
    pcfx_message_box(hwnd,
        "Command line options:\n\n"
        "  pcfx.exe [options] [image]\n\n"
        "  --fullscreen           Start in fullscreen. F11 or Alt+Enter toggles at runtime.\n"
        "  --windowed             Override saved fullscreen and start windowed.\n"
        "  --fullscreen-mode=exclusive|borderless\n"
        "                         Exclusive uses a real D3D11 display mode with borderless fallback.\n"
        "  --launchbox            Console-like mode: hide the menu, default to fullscreen,\n"
        "                         and exit when RUN + SELECT are held for 3 seconds.\n"
        "  --load <path>          Load a disc image or HuEXE path.\n"
        "  --bios                 Boot the BIOS instead of loading media.\n"
        "  --auto | --pcfx | --pcfxga\n"
        "                         Select BIOS/system mode for this launch.\n"
#if defined(PCFX_WIN32_HAVE_WASAPI)
        "  --audio=waveout | --audio=wasapi | --audio=wasapi-exclusive\n"
        "                         Override the saved audio backend.\n"
#else
        "  --audio=waveout       Override the saved audio backend.\n"
#endif
#if defined(PCFX_WIN32_HAVE_D3D11)
        "  --video=d3d11 | --video=gdi\n"
        "                         Override the saved video backend.\n"
#else
        "  --video=gdi           Override the saved video backend.\n"
#endif
        "  --xinput | --no-xinput Override XInput enable state.\n"
        "\n"
        "LaunchBox example:\n"
        "  pcfx.exe --launchbox --fullscreen \"C:\\Games\\PC-FX\\Game.cue\"",
        "PCFXEmu - Command line", MB_ICONINFORMATION | MB_OK);
}

static void request_open_game(HWND hwnd)
{
    if(!hwnd || g_open_hotkey_dialog_active)
        return;
    PostMessageA(hwnd, WM_PCFX_OPEN_GAME, 0, 0);
}

static void service_frontend_hotkeys(HWND hwnd)
{
    int pad_open_down;
    int combo = 0;
    LARGE_INTEGER now;

    if(!hwnd || g_open_hotkey_dialog_active)
        return;

    pad_open_down = g_open_hotkey_xinput_code &&
                    PCFX_Win32_InputXInputCodeDown(g_open_hotkey_xinput_player, g_open_hotkey_xinput_code);
    if(pad_open_down && !g_open_hotkey_pad_prev)
        request_open_game(hwnd);
    g_open_hotkey_pad_prev = pad_open_down;

    if(!g_launchbox_mode || !g_game_loaded)
    {
        g_launchbox_exit_combo_prev = 0;
        g_launchbox_exit_hold_start.QuadPart = 0;
        return;
    }

    for(int p = 0; p < PCFX_WIN32_PLAYERS; p++)
    {
        if(PCFX_Win32_InputButtonDown(p, 10) && PCFX_Win32_InputButtonDown(p, 11))
        {
            combo = 1;
            break;
        }
    }

    QueryPerformanceCounter(&now);
    if(combo)
    {
        if(!g_launchbox_exit_combo_prev || g_launchbox_exit_hold_start.QuadPart == 0)
            g_launchbox_exit_hold_start = now;
        if(now.QuadPart - g_launchbox_exit_hold_start.QuadPart >= (g_qpc_freq.QuadPart * 3))
        {
            exit_vb = 1;
            PostMessageA(hwnd, WM_CLOSE, 0, 0);
        }
    }
    else
    {
        g_launchbox_exit_hold_start.QuadPart = 0;
    }
    g_launchbox_exit_combo_prev = combo;
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg)
    {
        case WM_CREATE:
            PCFX_Win32_SetWindow(hwnd);
            PCFX_Win32_InputSetWindow(hwnd);
            return 0;
        case WM_PCFX_OPEN_GAME:
            browse_load(hwnd);
            return 0;
        case WM_COMMAND:
            handle_command(hwnd, LOWORD(wp));
            return 0;
        case WM_ACTIVATEAPP:
            g_window_active = wp ? 1 : 0;
            update_audio_mute_state();
            if(g_window_active)
                force_video_redraw(hwnd);
            break;
        case WM_ACTIVATE:
            g_window_active = (LOWORD(wp) == WA_INACTIVE) ? 0 : 1;
            update_audio_mute_state();
            if(g_window_active)
                force_video_redraw(hwnd);
            break;
        case WM_ENTERMENULOOP:
            g_menu_mute_depth++;
            update_audio_mute_state();
            break;
        case WM_EXITMENULOOP:
            if(g_menu_mute_depth > 0)
                g_menu_mute_depth--;
            update_audio_mute_state();
            break;
        case WM_ENTERSIZEMOVE:
            audio_transition_mute_begin();
            break;
        case WM_SYSKEYDOWN:
            if(wp == VK_RETURN)
            {
                toggle_fullscreen(hwnd);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if(wp == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))
            {
                toggle_fullscreen(hwnd);
                return 0;
            }
            if(wp == VK_F11 && !(lp & 0x40000000))
            {
                toggle_fullscreen(hwnd);
                return 0;
            }
            if((GetKeyState(VK_CONTROL) & 0x8000) && wp == 'O')
            {
                browse_load(hwnd);
                return 0;
            }
            if(g_open_hotkey_key && wp == g_open_hotkey_key && !(lp & 0x40000000))
            {
                browse_load(hwnd);
                return 0;
            }
            if(wp == VK_F5 && g_game_loaded)
            {
                PCFX_SoftReset();
                return 0;
            }
            if(wp == VK_F6)
            {
                change_save_slot(hwnd, -1);
                return 0;
            }
            if(wp == VK_F7)
            {
                state_menu_action(hwnd, 1);
                return 0;
            }
            if(wp == VK_F8)
            {
                state_menu_action(hwnd, 0);
                return 0;
            }
            if(wp == VK_F9)
            {
                change_save_slot(hwnd, 1);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            PCFX_Win32_Paint(hdc, &ps.rcPaint);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            g_window_minimized = (wp == SIZE_MINIMIZED) ? 1 : 0;
            if(wp != SIZE_MINIMIZED)
                PCFX_Win32_SetClientSize(LOWORD(lp), HIWORD(lp));
            update_audio_mute_state();
            return 0;
        case WM_WINDOWPOSCHANGED:
        case WM_DISPLAYCHANGE:
            force_video_redraw(hwnd);
            break;
        case WM_EXITSIZEMOVE:
            audio_transition_mute_end();
            force_video_redraw(hwnd);
            break;
        case WM_CLOSE:
            Clean();
            close_game();
            if(g_audio_open)
            {
                Audio_Close();
                g_audio_open = 0;
            }
            Video_Close();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    CommandLineOptions cmdopt;
    parse_command_line_options(&cmdopt);

    g_hinst = hInstance;
    QueryPerformanceFrequency(&g_qpc_freq);

    Init_Configuration();
    apply_command_line_options(&cmdopt);
    apply_core_options();
    Init_Video();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_PCFX_APP));
    if(!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = APP_CLASS_NAME;
    RegisterClassA(&wc);

    g_menu = build_menu();
    g_window_menu = g_menu;
    RECT rc = {0, 0, 512, 480};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, g_launchbox_mode ? FALSE : TRUE);
    g_hwnd = CreateWindowA(APP_CLASS_NAME, g_launchbox_mode ? "PCFXEmu - LaunchBox" : "PCFXEmu",
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           NULL, g_launchbox_mode ? NULL : g_menu, hInstance, NULL);
    if(!g_hwnd)
        return 1;
    if(wc.hIcon)
    {
        SendMessageA(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
        SendMessageA(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIcon);
    }
    update_menu_checks();
    apply_startup_window_size();
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    force_video_redraw(g_hwnd);
    if(g_start_fullscreen)
        toggle_fullscreen(g_hwnd);

    if(cmdopt.show_help)
        show_command_line_help(g_hwnd);
    if(cmdopt.boot_bios)
        boot_bios(g_hwnd);
    else if(cmdopt.load_path[0])
        load_game_path(g_hwnd, cmdopt.load_path);

    MSG msg;
    while(!exit_vb)
    {
        while(PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if(msg.message == WM_QUIT)
            {
                exit_vb = 1;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if(exit_vb)
            break;
        update_audio_mute_state();
        if(g_game_loaded)
        {
            throttle_frame();
            Emulation_Run();
            service_frontend_hotkeys(g_hwnd);
        }
        else
        {
            service_frontend_hotkeys(g_hwnd);
            MsgWaitForMultipleObjects(0, NULL, FALSE, 25, QS_ALLINPUT);
        }
    }

    if(IsWindow(g_hwnd))
        SendMessageA(g_hwnd, WM_CLOSE, 0, 0);
    return 0;
}
