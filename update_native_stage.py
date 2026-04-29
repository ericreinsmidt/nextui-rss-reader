from pathlib import Path
import stat

ROOT = Path("/Users/htpc/Projects/nextui-rss-reader")

files = {
    "src/main.cpp": """#include <SDL.h>
#include <cstdio>
#include <cstdlib>

static void log_line(const char* message) {
    const char* log_file = std::getenv("NEXTFEED_LOG_FILE");
    if (!log_file || !*log_file) {
        return;
    }

    FILE* f = std::fopen(log_file, "a");
    if (!f) {
        return;
    }

    std::fprintf(f, "%s\\n", message);
    std::fclose(f);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    log_line("==== nextfeed native binary starting ====");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        log_line("SDL_Init failed");
        log_line(SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "NextFeed",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        log_line("SDL_CreateWindow failed");
        log_line(SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        log_line("SDL_CreateRenderer failed");
        log_line(SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    log_line("SDL window and renderer created successfully");

    bool running = true;
    Uint32 start_ticks = SDL_GetTicks();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                running = false;
            }

            if (event.type == SDL_KEYDOWN) {
                running = false;
            }
        }

        SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);

        if (SDL_GetTicks() - start_ticks > 5000) {
            running = false;
        }

        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    log_line("==== nextfeed native binary exiting cleanly ====");
    return 0;
}
""",
    "ports/tg5040/Makefile": """# tg5040 Makefile for NextFeed

""" + ".PHONY: all sync-assets build clean\n" + """
ROOT_DIR := ../..
PAK_DIR := pak
BIN_DIR := $(PAK_DIR)/bin
ASSET_FEEDS_DIR := $(PAK_DIR)/assets/feeds
ASSET_THEMES_DIR := $(PAK_DIR)/assets/themes

TARGET := $(BIN_DIR)/nextfeed
SRC := $(ROOT_DIR)/src/main.cpp

CXX ?= aarch64-nextui-linux-gnu-g++
STRIP ?= aarch64-nextui-linux-gnu-strip

CXXFLAGS ?= -O2 -std=c++17
SDL_CFLAGS := -I$(SYSROOT)/usr/include/SDL2
SDL_LDFLAGS := -L$(SYSROOT)/usr/lib -lSDL2

all: sync-assets build
\t@echo "tg5040 pak prepared."

sync-assets:
\tmkdir -p $(ASSET_FEEDS_DIR) $(ASSET_THEMES_DIR) $(BIN_DIR)
\tcp $(ROOT_DIR)/assets/feeds/default_feeds.txt $(ASSET_FEEDS_DIR)/default_feeds.txt
\tcp $(ROOT_DIR)/assets/themes/default.theme $(ASSET_THEMES_DIR)/default.theme
\tcp $(ROOT_DIR)/pak.json $(PAK_DIR)/pak.json

build:
\tmkdir -p $(BIN_DIR)
\t$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $(TARGET) $(SRC) $(SDL_LDFLAGS)
\t$(STRIP) $(TARGET)

clean:
\trm -f $(TARGET)
\trm -f $(PAK_DIR)/pak.json
\trm -f $(ASSET_FEEDS_DIR)/default_feeds.txt
\trm -f $(ASSET_THEMES_DIR)/default.theme
""",
    "scripts/build_tg5040_docker.sh": """#!/bin/sh

set -eu

ROOT_DIR="/Users/htpc/Projects/nextui-rss-reader"

docker run --rm \\
  -v "${ROOT_DIR}:/workspace" \\
  -w /workspace/ports/tg5040 \\
  ghcr.io/loveretro/tg5040-toolchain \\
  make
""",
    "Makefile": """# Top-level Makefile for NextFeed

""" + ".PHONY: help tg5040 tg5040-docker package clean\n" + """
help:
\t@echo "Available targets:"
\t@echo "  make tg5040         - prepare tg5040 pak assets locally"
\t@echo "  make tg5040-docker  - build tg5040 binary inside toolchain container"
\t@echo "  make package        - create release zip"
\t@echo "  make clean          - remove generated artifacts"

tg5040:
\t$(MAKE) -C ports/tg5040 sync-assets

tg5040-docker:
\tsh scripts/build_tg5040_docker.sh

package:
\tsh scripts/package_pak.sh

clean:
\trm -rf dist release build out
\t$(MAKE) -C ports/tg5040 clean
"""
}

executable_files = {
    "scripts/build_tg5040_docker.sh",
}

for rel_path, content in files.items():
    path = ROOT / rel_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")

for rel_path in executable_files:
    path = ROOT / rel_path
    mode = path.stat().st_mode
    path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

print("Native stage files written.")
print("Next steps:")
print(f"  cd {ROOT}")
print("  python3 update_native_stage.py")
print("  make tg5040-docker")
