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

SDL_Surface *sdl_screen, *backbuffer, *pcfxbuffer;

uint16_t* __restrict__ internal_pix;
const uint32_t internal_pitch = 256;

#ifndef SCALING_SOFTWARE
#error "Funkey port needs SCALING_SOFTWARE to be defined"
#endif

#define SDL_FLAGS SDL_HWSURFACE | SDL_DOUBLEBUF

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
	SDL_Init(SDL_INIT_VIDEO);
	SDL_ShowCursor(0);
	
	sdl_screen = SDL_SetVideoMode(240, 240, 16, SDL_FLAGS);
	backbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16, 0,0,0,0);
	pcfxbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 256, 240, 16, 0,0,0,0);

	Set_Video_InGame();
}

void Set_Video_Menu()
{
	Clear_Video();
}

void Set_Video_InGame()
{
	Clear_Video();
	
	internal_pix = (uint16_t*)pcfxbuffer->pixels;
}

void Video_Close()
{
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
	if (pcfxbuffer)
	{
		SDL_FreeSurface(pcfxbuffer);
		backbuffer = NULL;
	}
	SDL_Quit();
}

void Update_Video_Menu()
{
	SDL_SoftStretch(backbuffer, NULL, sdl_screen, NULL);
	SDL_Flip(sdl_screen);
}

void Update_Video_Ingame()
{
	SDL_Rect rc;
	rc.x = -8;
	rc.y = 0;
	switch(option.fullscreen)
	{
		case 0:
			SDL_BlitSurface(pcfxbuffer, NULL, sdl_screen, &rc);
		break;
		default:
		case 1:
			SDL_SoftStretch(pcfxbuffer, NULL, sdl_screen, NULL);
		break;
	}
	SDL_Flip(sdl_screen);
}
