#include <stdio.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/soundcard.h>

#include "sound_output.h"

static int32_t oss_audio_fd = -1;

uint32_t Audio_Init()
{
	uint32_t channels = 2;
	uint32_t format = AFMT_S16_LE;
	uint32_t tmp = SOUND_OUTPUT_FREQUENCY;
	int32_t err_ret;

	oss_audio_fd = open("/dev/dsp", O_WRONLY);
	if (oss_audio_fd < 0)
	{
		printf("Couldn't open /dev/dsp.\n");
		return 1;
	}
	
	err_ret = ioctl(oss_audio_fd, SNDCTL_DSP_SPEED,&tmp);
	if (err_ret == -1)
	{
		printf("Could not set sound frequency\n");
		return 1;
	}
	err_ret = ioctl(oss_audio_fd, SNDCTL_DSP_CHANNELS, &channels);
	if (err_ret == -1)
	{
		printf("Could not set channels\n");
		return 1;
	}
	err_ret = ioctl(oss_audio_fd, SNDCTL_DSP_SETFMT, &format);
	if (err_ret == -1)
	{
		printf("Could not set sound format\n");
		return 1;
	}
	return 0;
}

void Audio_Write(int16_t* buffer, uint32_t buffer_size)
{
	write(oss_audio_fd, buffer, buffer_size * 4 );
}

void Audio_Close()
{
	if (oss_audio_fd >= 0)
	{
		close(oss_audio_fd);
		oss_audio_fd = -1;
	}
}
