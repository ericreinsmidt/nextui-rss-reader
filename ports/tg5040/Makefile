# tg5040 Makefile for NextFeed

.PHONY: all sync-assets build clean

ROOT_DIR := ../..
PAK_DIR := pak
BIN_DIR := $(PAK_DIR)/bin
ASSET_FEEDS_DIR := $(PAK_DIR)/assets/feeds
ASSET_THEMES_DIR := $(PAK_DIR)/assets/themes
ASSET_DATA_DIR := $(PAK_DIR)/assets/data

TARGET := $(BIN_DIR)/nextfeed
SRC := $(ROOT_DIR)/src/main.cpp

CXX ?= aarch64-nextui-linux-gnu-g++
STRIP ?= aarch64-nextui-linux-gnu-strip

CXXFLAGS ?= -O2 -std=c++17
SDL_CFLAGS := -I$(SYSROOT)/usr/include/SDL2
SDL_LDFLAGS := -L$(SYSROOT)/usr/lib -lSDL2 -lSDL2_ttf

all: sync-assets build
	@echo "tg5040 pak prepared."

sync-assets:
	mkdir -p $(ASSET_FEEDS_DIR) $(ASSET_THEMES_DIR) $(ASSET_DATA_DIR) $(BIN_DIR)
	cp $(ROOT_DIR)/assets/feeds/default_feeds.txt $(ASSET_FEEDS_DIR)/default_feeds.txt
	cp $(ROOT_DIR)/assets/themes/default.theme $(ASSET_THEMES_DIR)/default.theme
	cp $(ROOT_DIR)/assets/data/demo_content.txt $(ASSET_DATA_DIR)/demo_content.txt
	cp $(ROOT_DIR)/pak.json $(PAK_DIR)/pak.json

build:
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $(TARGET) $(SRC) $(SDL_LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -f $(TARGET)
	rm -f $(PAK_DIR)/pak.json
	rm -f $(ASSET_FEEDS_DIR)/default_feeds.txt
	rm -f $(ASSET_THEMES_DIR)/default.theme
	rm -f $(ASSET_DATA_DIR)/demo_content.txt
