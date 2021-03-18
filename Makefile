PRGNAME     = pcfx.elf
CC          = gcc
CXX 		= g++

#### Configuration

# Possible values : retrostone, rs97, rs90
PORT = sdl
# Possible values : alsa, oss, portaudio
SOUND_ENGINE = sdl
CHD = YES

#### End of Configuration

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"

INCLUDES	= -Ilibretro-common/include -Isrc
INCLUDES	+= -Ishell/headers -Ishell/video/$(PORT) -Ishell/audio -Ishell/scalers -Ishell/input/sdl -Ishell/fonts -Ishell/menu
INCLUDES	+= -Imednafen -I./mednafen/vb -I./mednafen/sound -I. -Ishell/emu -Imednafen/include -Ishell/input -Imednafen/video -Imednafen/hw_cpu
INCLUDES	+= -Ilibretro-common/include  -Imednafen/hw_sound

DEFINES		= -DLSB_FIRST -DINLINE="inline" -DINLINE="inline" -DNDEBUG -DWANT_STEREO_SOUND -DFRAMESKIP
DEFINES		+= -DWANT_16BPP -DFRONTEND_SUPPORTS_RGB565 -D_7ZIP_ST -DWANT_PCFX_EMU -DENABLE_JOYSTICKCODE
DEFINES		+= -DSIZEOF_DOUBLE=8 -DMEDNAFEN_VERSION=\"0.9.36.5\" -DPACKAGE=\"mednafen\" -DMEDNAFEN_VERSION_NUMERIC=9365 -DMPC_FIXED_POINT -DSTDC_HEADERS -D__STDC_LIMIT_MACROS -D__LIBRETRO__ -D_LOW_ACCURACY_
DEFINES		+= -DPACKAGE_VERSION=\"1.3.3\" -DHAVE_LROUND -DHAVE_STDINT_H -DHAVE_STDLIB_H -DHAVE_SYS_PARAM_H -DSCALING_SOFTWARE

ifeq ($(CHD), YES)
INCLUDES 		+= -Ideps/libchdr/include -Ideps/lzma-19.00/include
DEFINES			+= -DHAVE_CHD -D_7ZIP_ST
endif

CFLAGS		= -Ofast -g3 -fno-common -Wall -Wextra -Wunused-value $(INCLUDES) $(DEFINES)
CXXFLAGS	= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++14
LDFLAGS     = -lc -lgcc -lstdc++ -lm -lSDL -lz

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
SRCDIR		+= ./mednafen ./mednafen/cdrom ./mednafen/hw_sound/pce_psg ./mednafen/hw_video/huc6270 ./mednafen/pcfx ./mednafen/pcfx/input ./mednafen/sound ./mednafen/hw_cpu/v810 ./mednafen/hw_cpu/v810/fpu-new ./mednafen/video
SRCDIR		+= ./libretro-common/compat ./libretro-common/file
SRCDIR		+= ./libretro-common/streams ./libretro-common/string ./libretro-common/time
SRCDIR		+= ./libretro-common/vfs
ifeq ($(TREMOR), YES)
SRCDIR		+= ./mednafen/tremor
endif
ifeq ($(CHD), YES)
SRCDIR		+= ./deps/libchdr/src ./deps/lzma-19.00/src
endif

VPATH		= $(SRCDIR)
SRC_C		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
SRC_CPP		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.cpp))
OBJ_C		= $(notdir $(patsubst %.c, %.o, $(SRC_C)))
OBJ_CPP		= $(notdir $(patsubst %.cpp, %.o, $(SRC_CPP)))
OBJS		= $(OBJ_C) $(OBJ_CPP)

# Rules to make executable
$(PRGNAME): $(OBJS)  
	$(CC) $(CFLAGS) -std=gnu99 -o $(PRGNAME) $^ $(LDFLAGS)
	
$(OBJ_CPP) : %.o : %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
	
$(OBJ_C) : %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<
	
clean:
	rm -f $(PRGNAME)$(EXESUFFIX) *.o
