CC := clang
FT_CPU := ../freetribe/cpu

SDL_CFLAGS := $(shell /opt/homebrew/bin/sdl2-config --cflags)
SDL_LIBS   := $(shell /opt/homebrew/bin/sdl2-config --libs)

CFLAGS := -O2 -Wall $(SDL_CFLAGS) \
          -I$(FT_CPU)/src/apps/brain \
          -I$(FT_CPU)/src/api \
          -I$(FT_CPU)/src/common \
          -I$(FT_CPU)/src/user \
          -I$(FT_CPU)/src/kernel \
          -I$(FT_CPU)/src/kernel/device \
          -I$(FT_CPU)/src/kernel/service \
          -I$(FT_CPU)/lib \
          -I$(FT_CPU)/lib/UGUI \
          -I$(FT_CPU)/lib/UGUI/Fonts

LDFLAGS := $(SDL_LIBS) -framework CoreMIDI -framework CoreFoundation

SRCS := sim_main.c \
        $(FT_CPU)/src/apps/brain/seq_engine.c \
        $(FT_CPU)/src/apps/brain/brain_app.c \
        $(FT_CPU)/lib/UGUI/ugui.c \
        $(FT_CPU)/lib/UGUI/Fonts/font_5x8.c \
        $(FT_CPU)/lib/UGUI/Fonts/font_6x8.c

TARGET := cuartitribe_sim

all: $(TARGET) load_to_electribe midi_check

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

load_to_electribe: load_to_electribe.c
	$(CC) -O2 -Wall load_to_electribe.c -o load_to_electribe -framework CoreMIDI -framework CoreFoundation

midi_check: midi_check.c
	$(CC) -O2 -Wall midi_check.c -o midi_check -framework CoreMIDI -framework CoreFoundation

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
