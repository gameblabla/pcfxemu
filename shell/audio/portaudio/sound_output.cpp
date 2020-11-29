#include <portaudio.h>
#include <stdint.h>
#include <stdio.h>

#include "shared.h"

static PaStream *apu_stream;

uint32_t Audio_Init()
{
	Pa_Initialize();
	
	PaStreamParameters outputParameters;
	
	outputParameters.device = Pa_GetDefaultOutputDevice();
	
	if (outputParameters.device == paNoDevice) 
	{
		printf("No sound output\n");
		return 1;
	}

	outputParameters.channelCount = 2;
	outputParameters.sampleFormat = paInt16;
	outputParameters.hostApiSpecificStreamInfo = NULL;
	
	Pa_OpenStream( &apu_stream, NULL, &outputParameters, SOUND_OUTPUT_FREQUENCY, SOUND_SAMPLES_SIZE, paNoFlag, NULL, NULL);
	Pa_StartStream( apu_stream );
	
	return 0;
}

void Audio_Write(int16_t* buffer, uint32_t buffer_size)
{
	Pa_WriteStream( apu_stream, (uint8_t*)buffer, buffer_size);
}

void Audio_Close()
{
	//Pa_Close();
}
