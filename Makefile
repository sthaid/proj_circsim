TARGETS = circsim

CC = gcc
OUTPUT_OPTION=-MMD -MP -o $@
# CFLAGS_PROFILING = -pg
CFLAGS_SDL2 = $(shell sdl2-config --cflags)
CFLAGS = -g -O0 -Wall -Iutil $(CFLAGS_PROFILING) $(CFLAGS_SDL2)

SRC_CIRSIM = main.c \
             util/util_sdl.c \
             util/util_sdl_predefined_panes.c \
             util/util_jpeg.c \
             util/util_png.c \
             util/util_misc.c

OBJ_CIRSIM=$(SRC_CIRSIM:.c=.o)

DEP=$(SRC_CIRSIM:.c=.d)

#
# build rules
#

all: $(TARGETS)

circsim: $(OBJ_CIRSIM) 
	$(CC) -pthread -lrt -lm -lpng -ljpeg -lSDL2 -lSDL2_ttf -lSDL2_mixer $(CFLAGS_PROFILING) \
              -o $@ $(OBJ_CIRSIM)

-include $(DEP)

#
# clean rule
#

clean:
	rm -f $(TARGETS) $(OBJ_CIRSIM) $(DEP) gmon.out

