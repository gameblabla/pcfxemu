#include <SDL/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include "main.h"

#include "menu.h"
#include "config.h"
#include "shared.h"

uint8_t *keystate;
extern uint8_t exit_vb;
extern uint32_t emulator_state;

#ifdef ENABLE_JOYSTICKCODE
int32_t axis_input[4] = {0, 0, 0, 0};
#endif

static uint8_t* keys;

void Read_General_Input(void)
{
	SDL_Event event;
	keys = SDL_GetKeyState(NULL);

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym)
				{
					case SDLK_RCTRL:
					case SDLK_END:
						emulator_state = 1;
					break;
					default:
					break;
				}
			break;
			case SDL_KEYUP:
				switch(event.key.keysym.sym)
				{
					case SDLK_HOME:
						emulator_state = 1;
					break;
					default:
					break;
				}
			break;
			#ifdef ENABLE_JOYSTICKCODE
			case SDL_JOYAXISMOTION:
				if (event.jaxis.axis < 4)
				axis_input[event.jaxis.axis] = event.jaxis.value;
			break;
			#endif
			default:
			break;
		}
	}
}


uint16_t Read_Pad_Input(void)
{
	uint16_t button = 0;
	// UP
	if (keys[option.config_buttons[0] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[1] < -DEADZONE_AXIS
#endif
	) button |= 256;
	
	// DOWN
	if (keys[option.config_buttons[1] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[1] > DEADZONE_AXIS
#endif
	) button |= 1024;
	
	// LEFT
	if (keys[option.config_buttons[2] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[0] < -DEADZONE_AXIS
#endif
	) button |= 2048;
	
	// RIGHT
	if (keys[option.config_buttons[3] ] == SDL_PRESSED
#ifdef ENABLE_JOYSTICKCODE
	|| axis_input[0] > DEADZONE_AXIS
#endif
	) button |= 512;
	
	
#ifdef GKD350_BUG_INPUT
	if (keys[SDLK_RETURN] && keys[SDLK_ESCAPE])
	{
		emulator_state = 1;
	}
#endif
	
	// A
	if (keys[option.config_buttons[4] ] == SDL_PRESSED) button |= 1;
	// B
	if (keys[option.config_buttons[5] ] == SDL_PRESSED) button |= 2;
	// C
	if (keys[option.config_buttons[6] ] == SDL_PRESSED) button |= 4;
	
	// X
	if (keys[option.config_buttons[7] ] == SDL_PRESSED) button |= 8;
	// Y
	if (keys[option.config_buttons[8] ] == SDL_PRESSED) button |= 16;
	// Z
	if (keys[option.config_buttons[9] ] == SDL_PRESSED) button |= 32;
	
	
	/* Second axis is offset by one on the RG-350 */
	
	// Start
	if (keys[option.config_buttons[10] ] == SDL_PRESSED) button |= 128;
	
	// Select
	if (keys[option.config_buttons[11] ] == SDL_PRESSED) button |= 64;
	
	
	return button;
}


int32_t Read_Mouse_X(void)
{
#ifdef ENABLE_JOYSTICKCODE
	if (axis_input[0] > DEADZONE_AXIS || axis_input[0] < -DEADZONE_AXIS)
	{
		return axis_input[0]/2560;
	}
	return 0;
#else
	return 0;
#endif
}

int32_t Read_Mouse_Y(void)
{
#ifdef ENABLE_JOYSTICKCODE
	if (axis_input[1] > DEADZONE_AXIS || axis_input[1] < -DEADZONE_AXIS)
	{
		return axis_input[1]/2560;
	}
	return 0;
#else
	return 0;
#endif
}


uint16_t Read_Mouse_buttons(void)
{
	uint16_t button = 0;
	// Mouse Left button
	if (keys[option.config_buttons[12] ] == SDL_PRESSED) button |= 1;
	// Mouse Right button
	if (keys[option.config_buttons[13] ] == SDL_PRESSED) button |= 2;
	return button;
}
