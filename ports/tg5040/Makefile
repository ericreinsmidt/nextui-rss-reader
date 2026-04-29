# NextFeed — tg5040 cross-compile Makefile
# Called inside Docker container via build script.

CROSS_COMPILE ?= aarch64-nextui-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
SYSROOT := /opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr
PLATFORM_ID := tg5040

PKG_CONFIG := pkg-config
SDL_CFLAGS  := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2) -lSDL2_ttf -lSDL2_image

WORKSPACE  := /workspace
BUILD_DIR  ?= $(WORKSPACE)/build/tg5040
SRC        := $(WORKSPACE)/src/main.c
AP_INCLUDE := $(WORKSPACE)/third_party/apostrophe/include
DEPS       := $(AP_INCLUDE)/apostrophe.h $(AP_INCLUDE)/apostrophe_widgets.h

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	-mcpu=cortex-a53 -mtune=cortex-a53 \
	-DPLATFORM_TG5040 \
	-I$(AP_INCLUDE) \
	$(SDL_CFLAGS)

LDFLAGS := $(SDL_LDFLAGS) -lpthread -ldl -lm

.PHONY: all clean

all: $(BUILD_DIR)/nextfeed

$(BUILD_DIR)/nextfeed: $(SRC) $(DEPS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
