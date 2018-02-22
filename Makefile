TARGETS = model

CC = gcc
OUTPUT_OPTION=-MMD -MP -o $@
# CFLAGS_PROFILING = -pg
CFLAGS_SDL2 = $(shell sdl2-config --cflags)
CFLAGS = -g -O0 -Wall -Iutil $(CFLAGS_PROFILING) $(CFLAGS_SDL2)

SRC_MODEL = main.c \
            display.c \
            model.c \
            util/util_sdl.c \
            util/util_sdl_predefined_panes.c \
            util/util_jpeg.c \
            util/util_png.c \
            util/util_misc.c

OBJ_MODEL=$(SRC_MODEL:.c=.o)

DEP=$(SRC_MODEL:.c=.d)

#
# build rules
#

all: $(TARGETS)

model: $(OBJ_MODEL) 
	$(CC) -pthread -lrt -lm -lreadline -lpng -ljpeg -lSDL2 -lSDL2_ttf -lSDL2_mixer $(CFLAGS_PROFILING) \
              -o $@ $(OBJ_MODEL)

-include $(DEP)

#
# clean rule
#

clean:
	rm -f $(TARGETS) $(OBJ_MODEL) $(DEP) gmon.out

