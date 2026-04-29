from pathlib import Path
import stat

ROOT = Path("/Users/htpc/Projects/nextui-rss-reader")

makefile_text = "\n".join([
    "# tg5040 Makefile for NextFeed",
    "",
    ".PHONY: all sync-assets build clean",
    "",
    "ROOT_DIR :=" + " " + "../..",
    "PAK_DIR := pak",
    "BIN_DIR := $(PAK_DIR)/bin",
    "ASSET_FEEDS_DIR := $(PAK_DIR)/assets/feeds",
    "ASSET_THEMES_DIR := $(PAK_DIR)/assets/themes",
    "",
    "TARGET := $(BIN_DIR)/nextfeed",
    "SRC := $(ROOT_DIR)/src/main.cpp",
    "",
    "CXX ?= aarch64-nextui-linux-gnu-g++",
    "STRIP ?= aarch64-nextui-linux-gnu-strip",
    "",
    "CXXFLAGS ?= -O2 -std=c++17",
    "SDL_CFLAGS := -I$(SYSROOT)/usr/include/SDL2",
    "SDL_LDFLAGS := -L$(SYSROOT)/usr/lib -lSDL2 -lSDL2_ttf",
    "",
    "all: sync-assets build",
    '\t@echo "tg5040 pak prepared."',
    "",
    "sync-assets:",
    '\tmkdir -p $(ASSET_FEEDS_DIR) $(ASSET_THEMES_DIR) $(BIN_DIR)',
    '\tcp $(ROOT_DIR)/assets/feeds/default_feeds.txt $(ASSET_FEEDS_DIR)/default_feeds.txt',
    '\tcp $(ROOT_DIR)/assets/themes/default.theme $(ASSET_THEMES_DIR)/default.theme',
    '\tcp $(ROOT_DIR)/pak.json $(PAK_DIR)/pak.json',
    "",
    "build:",
    '\tmkdir -p $(BIN_DIR)',
    '\t$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $(TARGET) $(SRC) $(SDL_LDFLAGS)',
    '\t$(STRIP) $(TARGET)',
    "",
    "clean:",
    '\trm -f $(TARGET)',
    '\trm -f $(PAK_DIR)/pak.json',
    '\trm -f $(ASSET_FEEDS_DIR)/default_feeds.txt',
    '\trm -f $(ASSET_THEMES_DIR)/default.theme',
    "",
])

main_cpp = r'''#include <SDL.h>
#include <SDL_ttf.h>
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

    std::fprintf(f, "%s\n", message);
    std::fclose(f);
}

static SDL_Texture* render_text(
    SDL_Renderer* renderer,
    TTF_Font* font,
    const char* text,
    SDL_Color color,
    int* out_w,
    int* out_h
) {
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) {
        log_line("TTF_RenderUTF8_Blended failed");
        log_line(TTF_GetError());
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        log_line("SDL_CreateTextureFromSurface failed");
        log_line(SDL_GetError());
        SDL_FreeSurface(surface);
        return nullptr;
    }

    *out_w = surface->w;
    *out_h = surface->h;
    SDL_FreeSurface(surface);
    return texture;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    log_line("==== nextfeed text stage starting ====");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        log_line("SDL_Init failed");
        log_line(SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        log_line("TTF_Init failed");
        log_line(TTF_GetError());
        SDL_Quit();
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
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        log_line("SDL_CreateRenderer failed");
        log_line(SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    const char* font_path = "/mnt/SDCARD/.system/res/font1.ttf";
    TTF_Font* title_font = TTF_OpenFont(font_path, 42);
    TTF_Font* body_font = TTF_OpenFont(font_path, 24);

    if (!title_font || !body_font) {
        log_line("TTF_OpenFont failed");
        log_line(TTF_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    log_line("SDL and SDL_ttf initialized successfully");

    SDL_Color title_color{240, 240, 240, 255};
    SDL_Color body_color{180, 180, 200, 255};

    int title_w = 0, title_h = 0;
    int body_w = 0, body_h = 0;

    SDL_Texture* title_tex = render_text(
        renderer, title_font, "NextFeed", title_color, &title_w, &title_h
    );
    SDL_Texture* body_tex = render_text(
        renderer, body_font, "Press any button to exit", body_color, &body_w, &body_h
    );

    bool running = true;

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

        if (title_tex) {
            SDL_Rect dst{60, 60, title_w, title_h};
            SDL_RenderCopy(renderer, title_tex, nullptr, &dst);
        }

        if (body_tex) {
            SDL_Rect dst{60, 140, body_w, body_h};
            SDL_RenderCopy(renderer, body_tex, nullptr, &dst);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (title_tex) SDL_DestroyTexture(title_tex);
    if (body_tex) SDL_DestroyTexture(body_tex);

    TTF_CloseFont(title_font);
    TTF_CloseFont(body_font);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    log_line("==== nextfeed text stage exiting cleanly ====");
    return 0;
}
'''

(ROOT / "src/main.cpp").write_text(main_cpp, encoding="utf-8")
(ROOT / "ports/tg5040/Makefile").write_text(makefile_text, encoding="utf-8")

print("Text stage files written.")
print("Next steps:")
print(f"  cd {ROOT}")
print("  python3 update_text_stage.py")
print("  make tg5040-docker")
