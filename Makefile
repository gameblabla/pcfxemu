CSTD ?= -std=gnu11
PRGNAME     = pcfx.elf
CC          = gcc

#### Configuration

# Possible values : retrostone, rs97, rs90
PORT = sdl
# Possible values : alsa, oss, portaudio
SOUND_ENGINE = sdl
CHD = YES
FAST_VIDEO ?= NO

#### End of Configuration

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"

INCLUDES	= -Ipcfx-common/include -Isrc
INCLUDES	+= -Ishell/headers -Ishell/video/$(PORT) -Ishell/audio -Ishell/scalers -Ishell/input/sdl -Ishell/fonts -Ishell/menu
INCLUDES	+= -Imednafen -Imednafen/pcfx -I./mednafen/vb -I./mednafen/sound -I. -Ishell/emu -Imednafen/include -Ishell/input -Imednafen/video -Imednafen/hw_cpu
INCLUDES	+= -Ipcfx-common/include  -Imednafen/hw_sound

DEFINES		= -DLSB_FIRST -DNDEBUG -DWANT_STEREO_SOUND -DFRAMESKIP
DEFINES		+= -DWANT_16BPP -DFRONTEND_SUPPORTS_RGB565 -D_7ZIP_ST -DWANT_PCFX_EMU -DENABLE_JOYSTICKCODE
DEFINES		+= -DSIZEOF_DOUBLE=8 -DMEDNAFEN_VERSION=\"0.9.36.5\" -DPACKAGE=\"mednafen\" -DMEDNAFEN_VERSION_NUMERIC=9365 -DMPC_FIXED_POINT -DSTDC_HEADERS -D__STDC_LIMIT_MACROS -D_LOW_ACCURACY_
DEFINES		+= -DPACKAGE_VERSION=\"1.3.3\" -DHAVE_LROUND -DHAVE_STDINT_H -DHAVE_STDLIB_H -DHAVE_SYS_PARAM_H -DSCALING_SOFTWARE -DHAVE_HUC6273 -DPCFX_V810_ACCURATE_ONLY=1

ifeq ($(CHD), YES)
INCLUDES 		+= -Ideps/libchdr/include -Ideps/lzma-19.00/include
DEFINES			+= -DHAVE_CHD -D_7ZIP_ST
endif


ifeq ($(FAST_VIDEO), YES)
DEFINES += -DPCFX_FAST_VIDEO_DEFAULT=1
endif
CFLAGS		= -Ofast -g3 -fno-common -Wall -Wextra -Wunused-value $(INCLUDES) $(DEFINES)
LDFLAGS     = -lc -lgcc -lm -lSDL -lz

ifeq ($(SOUND_ENGINE), alsa)
LDFLAGS 		+= -lasound
endif
ifeq ($(SOUND_ENGINE), portaudio)
LDFLAGS 		+= -lasound -lportaudio
endif
ifeq ($(SOUND_ENGINE), pulse)
LDFLAGS 		+= -lpulse-simple -lportaudio
endif

# Files to be compiled
SRCDIR 		=  ./src ./shell ./shell/scalers ./shell/emu ./shell/menu
SRCDIR		+= ./shell/input/sdl/ ./shell/video/$(PORT) ./shell/audio/$(SOUND_ENGINE)
SRCDIR		+= ./mednafen ./mednafen/cdrom ./mednafen/hw_sound/pce_psg ./mednafen/hw_video/huc6270 ./mednafen/pcfx ./mednafen/pcfx/huc6273 ./mednafen/pcfx/input ./mednafen/sound ./mednafen/hw_cpu/v810 ./mednafen/hw_cpu/v810/fpu-new ./mednafen/video
SRCDIR		+= ./pcfx-common/compat
ifeq ($(TREMOR), YES)
SRCDIR		+= ./mednafen/tremor
endif
ifeq ($(CHD), YES)
SRCDIR		+= ./deps/libchdr/src ./deps/lzma-19.00/src
endif

VPATH		= $(SRCDIR)
SRC_C		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
OBJ_C		= $(notdir $(patsubst %.c, %.o, $(SRC_C)))
OBJS		= $(OBJ_C)

# Rules to make executable
$(PRGNAME): $(OBJS)
	$(CC) $(CFLAGS) $(CSTD) -o $(PRGNAME) $^ $(LDFLAGS)

$(OBJ_C) : %.o : %.c
	$(CC) $(CFLAGS) $(CSTD) -c -o $@ $<

clean:
	rm -f $(PRGNAME)$(EXESUFFIX) *.o
