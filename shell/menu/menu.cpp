#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libgen.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <SDL/SDL.h>

#include "scaler.h"
#include "font_drawing.h"
#include "sound_output.h"
#include "video_blit.h"
#include "config.h"
#include "menu.h"
#include "main.h"

t_config option;
uint32_t emulator_state = 0;

extern uint8_t exit_vb;

char home_path[128], save_path[192], sram_path[192], conf_path[192];

extern SDL_Surface *sdl_screen;
extern char GameName_emu[256];

static uint8_t save_slot = 0;
#ifdef SCALING_SOFTWARE
static const int8_t upscalers_available = 1
#ifdef SCALE2X_UPSCALER
+1
#endif
;
#endif

static void SaveState_Menu(uint_fast8_t load_mode, uint_fast8_t slot)
{
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s/%s_%d.sts", save_path, GameName_emu, slot);
	SaveState(tmp,load_mode);
}

static void SRAM_Menu(uint_fast8_t load_mode)
{
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s/%s.srm", sram_path, GameName_emu);
	SRAM_Save(tmp,load_mode);
}


static void config_load()
{
	char config_path[512];
	FILE* fp;
	snprintf(config_path, sizeof(config_path), "%s/%s.cfg", conf_path, GameName_emu);

	fp = fopen(config_path, "rb");
	if (fp)
	{
		fread(&option, sizeof(option), sizeof(int8_t), fp);
		fclose(fp);
	}
	else
	{
		/* Default mapping for Horizontal */
		option.config_buttons[0] = SDLK_UP; // UP
		option.config_buttons[1] = SDLK_DOWN; // DOWN
		option.config_buttons[2] = SDLK_LEFT; // LEFT
		option.config_buttons[3] = SDLK_RIGHT; // RIGHT

		option.config_buttons[4] = SDLK_LCTRL; // A
		option.config_buttons[5] = SDLK_LALT; // B
		option.config_buttons[6] = SDLK_LSHIFT; // C
		
		option.config_buttons[7] = SDLK_SPACE; // X
		option.config_buttons[8] = SDLK_TAB; // Y
		option.config_buttons[9] = SDLK_BACKSPACE; // Z
		
		/* Default mapping for Horizontal */
		option.config_buttons[10] = SDLK_RETURN;
		option.config_buttons[11] = SDLK_ESCAPE;
		option.config_buttons[12] = SDLK_LCTRL;
		option.config_buttons[13] = SDLK_LALT;

		option.fullscreen = 1;
		option.type_controller = 0;
	}
}

static void config_save()
{
	FILE* fp;
	char config_path[512];
	snprintf(config_path, sizeof(config_path), "%s/%s.cfg", conf_path, GameName_emu);

	fp = fopen(config_path, "wb");
	if (fp)
	{
		fwrite(&option, sizeof(option), sizeof(int8_t), fp);
		fclose(fp);
	}
}

static const char* Return_Text_Button(uint32_t button)
{
	switch(button)
	{
		/* UP button */
		case SDLK_UP:
			return "DPAD UP";
		break;
		/* DOWN button */
		case SDLK_DOWN:
			return "DPAD DOWN";
		break;
		/* LEFT button */
		case SDLK_LEFT:
			return "DPAD LEFT";
		break;
		/* RIGHT button */
		case SDLK_RIGHT:
			return "DPAD RIGHT";
		break;
		/* A button */
		case SDLK_LCTRL:
			return "A";
		break;
		/* B button */
		case SDLK_LALT:
			return "B";
		break;
		/* X button */
		case SDLK_LSHIFT:
			return "X";
		break;
		/* Y button */
		case SDLK_SPACE:
			return "Y";
		break;
		/* L button */
		case SDLK_TAB:
			return "L";
		break;
		/* R button */
		case SDLK_BACKSPACE:
			return "R";
		break;
		case SDLK_PAGEUP:
			return "L2";
		break;
		case SDLK_PAGEDOWN:
			return "R2";
		break;
		case SDLK_KP_DIVIDE:
			return "L3";
		break;
		case SDLK_KP_PERIOD:
			return "R3";
		break;
		/* Start */
		case SDLK_RETURN:
			return "Start";
		break;
		/* Select */
		case SDLK_ESCAPE:
			return "Select";
		break;
		default:
			return "Unknown";
		break;
		case 0:
			return "...";
		break;
	}
}

        
void Draw_Option(uint32_t numb, uint32_t selection, const char* drawtext, uint32_t x, uint32_t y)
{
	char text[18];
	snprintf(text, sizeof(text), drawtext, Return_Text_Button(option.config_buttons[numb]));
	if (selection == numb+1) print_string(text, TextRed, 0, x, y+2, (uint16_t*)backbuffer->pixels);
	else print_string(text, TextWhite, 0, x, y+2, (uint16_t*)backbuffer->pixels);
}

static void Input_Remapping()
{
	SDL_Event Event;
	uint32_t pressed = 0;
	int32_t currentselection = 1;
	int32_t exit_input = 0;
	uint32_t exit_map = 0;

	while(!exit_input)
	{
		SDL_FillRect( backbuffer, NULL, 0 );

        while (SDL_PollEvent(&Event))
        {
            if (Event.type == SDL_KEYDOWN)
            {
                switch(Event.key.keysym.sym)
                {
                    case SDLK_UP:
                        currentselection--;
                        if (currentselection < 1)
                        {
							currentselection = 14;
						}
                        break;
                    case SDLK_DOWN:
                        currentselection++;
                        if (currentselection == 15)
                        {
							currentselection = 1;
						}
                        break;
                    case SDLK_LCTRL:
                    case SDLK_RETURN:
                        pressed = 1;
					break;
                    case SDLK_ESCAPE:
                        option.config_buttons[currentselection - 1] = 0;
					break;
                    case SDLK_LALT:
                        exit_input = 1;
					break;
                    case SDLK_LEFT:
						if (currentselection > 8) currentselection -= 8;
					break;
                    case SDLK_RIGHT:
						if (currentselection < 9) currentselection += 8;
					break;
					default:
					break;
                }
            }
        }

        if (pressed)
        {
			SDL_Delay(1);
            switch(currentselection)
            {
                default:
					exit_map = 0;
					while( !exit_map )
					{
						SDL_FillRect( backbuffer, NULL, 0 );
						print_string("Press button to map", TextWhite, TextBlue, 92, 108, (uint16_t*)backbuffer->pixels);

						while (SDL_PollEvent(&Event))
						{
							if (Event.type == SDL_KEYDOWN)
							{
								if (Event.key.keysym.sym != SDLK_RCTRL || Event.key.keysym.sym != SDLK_HOME)
								{
									option.config_buttons[currentselection - 1] = Event.key.keysym.sym;
									exit_map = 1;
									pressed = 0;
								}
							}
						}
						Update_Video_Menu();
					}
				break;
            }
        }

        if (currentselection > 14) currentselection = 14;

		print_string("[A] = Map, [B] = Exit", TextWhite, TextBlue, 50, 210, (uint16_t*)backbuffer->pixels);

		Draw_Option(0, currentselection, "UP   : %s\n", 5, 25);
		Draw_Option(1, currentselection, "DOWN : %s\n", 5, 45);
		Draw_Option(2, currentselection, "LEFT : %s\n", 5, 65);
		Draw_Option(3, currentselection, "RIGHT: %s\n", 5, 85);
		
		Draw_Option(4, currentselection, "A    : %s\n", 5, 105);
		Draw_Option(5, currentselection, "B    : %s\n", 5, 125);
		Draw_Option(6, currentselection, "C    : %s\n", 5, 145);
		Draw_Option(7, currentselection, "X    : %s\n", 5, 165);
		
		Draw_Option(8, currentselection, "Y      : %s\n", 165, 25);
		Draw_Option(9, currentselection, "Z      : %s\n", 165, 45);
		Draw_Option(10, currentselection,"START  : %s\n", 165, 65);
		Draw_Option(11, currentselection,"SLECT  : %s\n", 165, 85);
		
		Draw_Option(12, currentselection,"MOUSE 1: %s\n", 165, 105);
		Draw_Option(13, currentselection,"MOUSE 2: %s\n", 165, 125);	

		Update_Video_Menu();
	}

	config_save();
}

#ifdef SCALING_SOFTWARE
#define SCALING_SOFTWARE_OFFSET 0
#else
#define SCALING_SOFTWARE_OFFSET 1
#endif

void Menu()
{
	char text[28];
    int16_t pressed = 0;
    int16_t currentselection = 1;
    SDL_Event Event;

    Set_Video_Menu();

	/* Save sram settings each time we bring up the menu */
	SRAM_Menu(0);

    while (((currentselection != 1) && (currentselection != 7-SCALING_SOFTWARE_OFFSET)) || (!pressed))
    {
        pressed = 0;

        SDL_FillRect( backbuffer, NULL, 0 );

		print_string("PCFXEmu - Built on " __DATE__, TextWhite, 0, 5, 15, (uint16_t*)backbuffer->pixels);

		Draw_Option(0, currentselection, "Continue", 5, 45);

		snprintf(text, sizeof(text), "Load State %d", save_slot);
		Draw_Option(1, currentselection, text, 5, 65);

		snprintf(text, sizeof(text), "Save State %d", save_slot);
		Draw_Option(2, currentselection, text, 5, 85);

		#ifdef SCALING_SOFTWARE
		switch(option.fullscreen)
		{
			case 0:
				snprintf(text, sizeof(text), "Scaling : Native");
			break;
			case 1:
				snprintf(text, sizeof(text), "Scaling : Stretched");
			break;
			case 2:
				snprintf(text, sizeof(text), "Scaling : EPX/Scale2x");
			break;
		}
		Draw_Option(3-SCALING_SOFTWARE_OFFSET, currentselection-SCALING_SOFTWARE_OFFSET, text, 5, 105);
		#endif

		switch(option.type_controller)
		{
			default:
				snprintf(text, sizeof(text), "PAD Type : Pad");
			break;
			case 1:
				snprintf(text, sizeof(text), "PAD Type : Mouse");
			break;
		}
		Draw_Option(4-SCALING_SOFTWARE_OFFSET, currentselection, text, 5, 125);
		Draw_Option(5-SCALING_SOFTWARE_OFFSET, currentselection, "Input remapping", 5, 145);
		Draw_Option(6-SCALING_SOFTWARE_OFFSET, currentselection, "Quit", 5, 165);

		print_string("Libretro Fork by gameblabla", TextWhite, 0, 5, 205, (uint16_t*)backbuffer->pixels);
		print_string("Credits: Ryphecha, libretro", TextWhite, 0, 5, 225, (uint16_t*)backbuffer->pixels);

        while (SDL_PollEvent(&Event))
        {
            if (Event.type == SDL_KEYDOWN)
            {
                switch(Event.key.keysym.sym)
                {
                    case SDLK_UP:
                        currentselection--;
                        if (currentselection == 0)
                            currentselection = 7-SCALING_SOFTWARE_OFFSET;
                        break;
                    case SDLK_DOWN:
                        currentselection++;
                        if (currentselection == 8-SCALING_SOFTWARE_OFFSET)
                            currentselection = 1;
                        break;
                    case SDLK_END:
                    case SDLK_RCTRL:
                    case SDLK_LALT:
						pressed = 1;
						currentselection = 1;
						break;
                    case SDLK_LCTRL:
                    case SDLK_RETURN:
                        pressed = 1;
                        break;
                    case SDLK_LEFT:
                        switch(currentselection)
                        {
                            case 2:
                            case 3:
                                if (save_slot > 0) save_slot--;
							break;
							#ifdef SCALING_SOFTWARE
                            case 4:
							option.fullscreen--;
							if (option.fullscreen < 0)
								option.fullscreen = upscalers_available;
							break;
							#endif
							case 5-SCALING_SOFTWARE_OFFSET:
							option.type_controller--;
							if (option.type_controller > 2)
								option.type_controller = 1;
							break;
                        }
                        break;
                    case SDLK_RIGHT:
                        switch(currentselection)
                        {
                            case 2:
                            case 3:
                                save_slot++;
								if (save_slot == 10)
									save_slot = 9;
							break;
							#ifdef SCALING_SOFTWARE
                            case 4:
                                option.fullscreen++;
                                if (option.fullscreen > upscalers_available)
                                    option.fullscreen = 0;
							break;
							#endif
							case 5-SCALING_SOFTWARE_OFFSET:
							option.type_controller++;
							if (option.type_controller > 1)
								option.type_controller = 0;
							break;
                        }
                        break;
					default:
					break;
                }
            }
            else if (Event.type == SDL_QUIT)
            {
				currentselection = 7-SCALING_SOFTWARE_OFFSET;
				pressed = 1;
			}
        }

        if (pressed)
        {
            switch(currentselection)
            {
				case 6-SCALING_SOFTWARE_OFFSET:
					Input_Remapping();
				break;
				#ifdef SCALING_SOFTWARE
                case 4 :
                    option.fullscreen++;
                    if (option.fullscreen > upscalers_available)
                        option.fullscreen = 0;
                    break;
                #endif
                case 2 :
                    SaveState_Menu(1, save_slot);
					currentselection = 1;
                    break;
                case 3 :
					SaveState_Menu(0, save_slot);
					currentselection = 1;
				break;
				default:
				break;
            }
        }

		Update_Video_Menu();
    }

	Clear_Video();

    if (currentselection == 7-SCALING_SOFTWARE_OFFSET)
    {
        exit_vb = 1;
	}

	/* Switch back to emulator core */
	emulator_state = 0;
	Set_Video_InGame();
}

void Init_Configuration()
{
	snprintf(home_path, sizeof(home_path), "%s/.pcfxemu", getenv("HOME"));

	snprintf(conf_path, sizeof(conf_path), "%s/conf", home_path);
	snprintf(save_path, sizeof(save_path), "%s/sstates", home_path);
	snprintf(sram_path, sizeof(sram_path), "%s/sram", home_path);

	/* We check first if folder does not exist.
	 * Let's only try to create it if so in order to decrease boot times.
	 * */

	if (access( home_path, F_OK ) == -1)
	{
		mkdir(home_path, 0755);
	}

	if (access( save_path, F_OK ) == -1)
	{
		mkdir(save_path, 0755);
	}

	if (access( conf_path, F_OK ) == -1)
	{
		mkdir(conf_path, 0755);
	}

	if (access( sram_path, F_OK ) == -1)
	{
		mkdir(sram_path, 0755);
	}
	
	config_load();
}

void Load_Configuration()
{
	/* Load sram file if it exists */
	SRAM_Menu(1);
}

void Clean(void)
{
	config_save();
}
