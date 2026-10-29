ZIG ?= ../tools/zig/zig
DG  := ../refs/doomgeneric/doomgeneric
OUT := build/kdoom

TARGET := arm-linux-musleabihf

CFLAGS := -std=gnu11 -O2 -fno-strict-aliasing -ffunction-sections -fdata-sections \
	-D_DEFAULT_SOURCE -DNORMALUNIX -DLINUX \
	-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
	-I$(DG) -I.

LDFLAGS := -static -Wl,--gc-sections

ENGINE := $(filter-out \
	$(DG)/doomgeneric_allegro.c \
	$(DG)/doomgeneric_emscripten.c \
	$(DG)/doomgeneric_linuxvt.c \
	$(DG)/doomgeneric_sdl.c \
	$(DG)/doomgeneric_soso.c \
	$(DG)/doomgeneric_sosox.c \
	$(DG)/doomgeneric_win.c \
	$(DG)/doomgeneric_xlib.c \
	$(DG)/i_allegromusic.c \
	$(DG)/i_allegrosound.c \
	$(DG)/i_sdlmusic.c \
	$(DG)/i_sdlsound.c \
	$(DG)/mus2mid.c \
	$(DG)/gusconf.c \
	$(DG)/icon.c \
	,$(wildcard $(DG)/*.c))

.PHONY: all clean
all: $(OUT)

$(OUT): doomgeneric_kindle.c $(ENGINE) font8x8_basic.h
	mkdir -p build
	$(ZIG) cc -target $(TARGET) $(CFLAGS) $(LDFLAGS) \
		doomgeneric_kindle.c $(ENGINE) -lm -o $(OUT)
	file $(OUT)
	ls -lh $(OUT)

clean:
	rm -rf build
