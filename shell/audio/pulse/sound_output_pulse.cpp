/*
 * Pulseaudio output sound code.
 * License : MIT
 * See docs/MIT_license.txt for more information.
*/

#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/unistd.h>
#include <pulse/simple.h>
#include "sound_output.h"
#include "shared.h"

static pa_simple *pulse_stream;

uint32_t Audio_Init()
{
	pa_sample_spec ss;
	pa_buffer_attr paattr;
	
	ss.format = PA_SAMPLE_S16LE;
	ss.channels = 2;
	ss.rate = SOUND_OUTPUT_FREQUENCY;

	paattr.tlength = SOUND_SAMPLES_SIZE * 4;
	paattr.prebuf = -1;
	paattr.maxlength = -1;
	paattr.minreq = SOUND_SAMPLES_SIZE;

	/* Create a new playback stream */
	pulse_stream = pa_simple_new(NULL, "VBEmu", PA_STREAM_PLAYBACK, NULL, "VBEmu", &ss, NULL, &paattr, NULL);
	if (!pulse_stream)
	{
		fprintf(stderr, "PulseAudio: pa_simple_new() failed!\n");
	}
	return 1;
}

void Audio_Write(int16_t* buffer, uint32_t buffer_size)
{
	if (pa_simple_write(pulse_stream, buffer, buffer_size * 4, NULL) < 0)
	{
		fprintf(stderr, "PulseAudio: pa_simple_write() failed!\n");
	}
}

void Audio_Close()
{
	if(pulse_stream != NULL)
	{
		if (pa_simple_drain(pulse_stream, NULL) < 0) 
		{
			fprintf(stderr, "PulseAudio: pa_simple_drain() failed!\n");
		}
		pa_simple_free(pulse_stream);
	}
}
