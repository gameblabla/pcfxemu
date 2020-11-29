#include <sys/ioctl.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <SDL/SDL.h>

#include "sound_output.h"

static int32_t BUFFSIZE;
static uint8_t *buffer;
static int32_t buf_read_pos = 0;
static uint32_t buf_write_pos = 0;
static int32_t buffered_bytes = 0;

static int32_t sdl_read_buffer(uint8_t* data, int32_t len)
{
	if (buffered_bytes >= len) 
	{
		if(buf_read_pos + len <= BUFFSIZE ) 
		{
			memcpy(data, buffer + buf_read_pos, len);
		} 
		else 
		{
			int32_t tail = BUFFSIZE - buf_read_pos;
			memcpy(data, buffer + buf_read_pos, tail);
			memcpy(data + tail, buffer, len - tail);
		}
		buf_read_pos = (buf_read_pos + len) % BUFFSIZE;
		buffered_bytes -= len;
	}

	return len;
}


static void sdl_write_buffer(uint8_t* data, int32_t len)
{
	for(int32_t i = 0; i < len; i += 4) 
	{
		if(buffered_bytes == BUFFSIZE) return; // just drop samples
		*(int32_t*)((char*)(buffer + buf_write_pos)) = *(int32_t*)((char*)(data + i));
		//memcpy(buffer + buf_write_pos, data + i, 4);
		buf_write_pos = (buf_write_pos + 4) % BUFFSIZE;
		buffered_bytes += 4;
	}
}

void sdl_callback(void *unused, uint8_t *stream, int32_t len)
{
	sdl_read_buffer((uint8_t *)stream, len);
}
uint32_t Audio_Init()
{
	SDL_AudioSpec aspec, obtained;

	BUFFSIZE = (SOUND_SAMPLES_SIZE * 2 * 2) * 4;
	buffer = (uint8_t *) malloc(BUFFSIZE);

	/* Add some silence to the buffer */
	buffered_bytes = 0;
	buf_read_pos = 0;
	buf_write_pos = 0;

	aspec.format   = AUDIO_S16SYS;
	aspec.freq     = SOUND_OUTPUT_FREQUENCY;
	aspec.channels = 2;
	aspec.samples  = SOUND_SAMPLES_SIZE;
	aspec.callback = (sdl_callback);
	aspec.userdata = NULL;

	/* initialize the SDL Audio system */
	if (SDL_InitSubSystem (SDL_INIT_AUDIO | SDL_INIT_NOPARACHUTE)) 
	{
		printf("SDL: Initializing of SDL Audio failed: %s.\n", SDL_GetError());
		return 1;
	}

	/* Open the audio device and start playing sound! */
	if(SDL_OpenAudio(&aspec, &obtained) < 0) 
	{
		printf("SDL: Unable to open audio: %s\n", SDL_GetError());
		return 1;
	}
	
	SDL_PauseAudio(0);
	
	return 0;
}

void Audio_Write(int16_t* buffer, uint32_t buffer_size)
{
	SDL_LockAudio();
	sdl_write_buffer((uint8_t*)buffer, buffer_size * 4);
	SDL_UnlockAudio();
}

void Audio_Close()
{
	SDL_PauseAudio(1);
	SDL_CloseAudio();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	if (buffer)
	{
		free(buffer);
		buffer = NULL;
	}
}
