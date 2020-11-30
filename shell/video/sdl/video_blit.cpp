/* Cygne
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Dox dox@space.pl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL/SDL.h>
#include <sys/time.h>
#include <sys/types.h>

#include "video_blit.h"
#include "scaler.h"
#include "config.h"

SDL_Surface *sdl_screen, *backbuffer;

uint16_t* __restrict__ internal_pix;
const uint32_t internal_pitch = 320;

#ifndef SDL_TRIPLEBUF
#define SDL_TRIPLEBUF SDL_DOUBLEBUF
#endif

/* GCW0 : Triple buffering causes flickering, for some reasons... */
#define SDL_FLAGS SDL_HWSURFACE | SDL_TRIPLEBUF

#ifdef ENABLE_JOYSTICKCODE
SDL_Joystick* sdl_joy;
#endif

void Clear_Video()
{
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
	SDL_FillRect(sdl_screen, NULL, 0);
	SDL_Flip(sdl_screen);
}


void Init_Video()
{
	#ifdef ENABLE_JOYSTICKCODE
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
	
	if (SDL_NumJoysticks() > 0)
	{
		sdl_joy = SDL_JoystickOpen(0);
		SDL_JoystickEventState(SDL_ENABLE);
	}
	#else
	SDL_Init(SDL_INIT_VIDEO);
	#endif

	SDL_ShowCursor(0);
	
	sdl_screen = SDL_SetVideoMode(320, 240, 16, SDL_FLAGS);
	backbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16, 0,0,0,0);

	Set_Video_InGame();
}

void Set_Video_Menu()
{
	Clear_Video();
}

void Set_Video_InGame()
{
	Clear_Video();
	internal_pix = (uint16_t*)sdl_screen->pixels + 32;
}

void Video_Close()
{
	#ifdef ENABLE_JOYSTICKCODE
	SDL_JoystickClose(sdl_joy);
	#endif
	if (sdl_screen)
	{
		SDL_FreeSurface(sdl_screen);
		sdl_screen = NULL;
	}
	if (backbuffer)
	{
		SDL_FreeSurface(backbuffer);
		backbuffer = NULL;
	}
	SDL_Quit();
}

void Update_Video_Menu()
{
	SDL_BlitSurface(backbuffer, NULL, sdl_screen, NULL);
	SDL_Flip(sdl_screen);
}

void Update_Video_Ingame()
{
	SDL_Flip(sdl_screen);
}
