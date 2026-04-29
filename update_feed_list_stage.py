from pathlib import Path

ROOT = Path("/Users/htpc/Projects/nextui-rss-reader")

main_cpp = r'''#include <SDL.h>
#include <SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static void log_line(const std::string& message) {
    const char* log_file = std::getenv("NEXTFEED_LOG_FILE");
    if (!log_file || !*log_file) return;
    FILE* f = std::fopen(log_file, "a");
    if (!f) return;
    std::fprintf(f, "%s\n", message.c_str());
    std::fclose(f);
}

static SDL_Texture* render_text(
    SDL_Renderer* renderer,
    TTF_Font* font,
    const std::string& text,
    SDL_Color color,
    int* out_w,
    int* out_h
) {
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return nullptr;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return nullptr;
    }

    *out_w = surface->w;
    *out_h = surface->h;
    SDL_FreeSurface(surface);
    return texture;
}

static void draw_text(
    SDL_Renderer* renderer,
    TTF_Font* font,
    const std::string& text,
    SDL_Color color,
    int x,
    int y
) {
    int w = 0;
    int h = 0;
    SDL_Texture* tex = render_text(renderer, font, text, color, &w, &h);
    if (!tex) return;

    SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    log_line("==== nextfeed feed list stage starting ====");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
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

    SDL_JoystickEventState(SDL_ENABLE);

    SDL_Joystick* joystick = nullptr;
    if (SDL_NumJoysticks() > 0) {
        joystick = SDL_JoystickOpen(0);
        if (joystick) {
            const char* name = SDL_JoystickName(joystick);
            log_line(std::string("Joystick 0 opened: ") + (name ? name : "(unknown)"));
        }
    }

    SDL_Window* window = SDL_CreateWindow(
        "NextFeed",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1024,
        768,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        log_line("SDL_CreateWindow failed");
        log_line(SDL_GetError());
        if (joystick) SDL_JoystickClose(joystick);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        log_line("SDL_CreateRenderer failed");
        log_line(SDL_GetError());
        SDL_DestroyWindow(window);
        if (joystick) SDL_JoystickClose(joystick);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    const char* font_path = "/mnt/SDCARD/.system/res/font1.ttf";
    TTF_Font* title_font = TTF_OpenFont(font_path, 40);
    TTF_Font* item_font = TTF_OpenFont(font_path, 24);
    TTF_Font* hint_font = TTF_OpenFont(font_path, 20);

    if (!title_font || !item_font || !hint_font) {
        log_line("TTF_OpenFont failed");
        log_line(TTF_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        if (joystick) SDL_JoystickClose(joystick);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    std::vector<std::string> feeds = {
        "Hacker News",
        "Planet GNOME",
        "LWN Headlines"
    };

    int selected = 0;
    bool running = true;
    bool show_open_message = false;
    Uint32 open_message_until = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            if (event.type == SDL_JOYHATMOTION && event.jhat.hat == 0) {
                if (event.jhat.value == 1) {
                    selected--;
                    if (selected < 0) selected = static_cast<int>(feeds.size()) - 1;
                    log_line("Move up");
                } else if (event.jhat.value == 4) {
                    selected++;
                    if (selected >= static_cast<int>(feeds.size())) selected = 0;
                    log_line("Move down");
                }
            }

            if (event.type == SDL_JOYBUTTONDOWN) {
                if (event.jbutton.button == 1) {
                    show_open_message = true;
                    open_message_until = SDL_GetTicks() + 1200;
                    log_line("Open feed");
                } else if (event.jbutton.button == 0) {
                    log_line("Back/exit");
                    running = false;
                } else if (event.jbutton.button == 7) {
                    log_line("Start/exit");
                    running = false;
                }
            }
        }

        if (show_open_message && SDL_GetTicks() > open_message_until) {
            show_open_message = false;
        }

        SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
        SDL_RenderClear(renderer);

        SDL_Color title_color{240, 240, 240, 255};
        SDL_Color normal_color{180, 180, 200, 255};
        SDL_Color selected_color{255, 220, 120, 255};
        SDL_Color hint_color{120, 180, 220, 255};

        draw_text(renderer, title_font, "NextFeed", title_color, 40, 40);
        draw_text(renderer, hint_font, "D-pad: move   A: open   B: exit   Start: exit", hint_color, 40, 100);

        for (size_t i = 0; i < feeds.size(); ++i) {
            SDL_Color color = (static_cast<int>(i) == selected) ? selected_color : normal_color;

            if (static_cast<int>(i) == selected) {
                SDL_SetRenderDrawColor(renderer, 50, 50, 70, 255);
                SDL_Rect highlight{30, 150 + static_cast<int>(i) * 60, 700, 42};
                SDL_RenderFillRect(renderer, &highlight);
            }

            draw_text(renderer, item_font, feeds[i], color, 50, 158 + static_cast<int>(i) * 60);
        }

        if (show_open_message) {
            draw_text(renderer, hint_font, "Article list coming soon...", hint_color, 40, 380);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    TTF_CloseFont(title_font);
    TTF_CloseFont(item_font);
    TTF_CloseFont(hint_font);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (joystick) SDL_JoystickClose(joystick);
    TTF_Quit();
    SDL_Quit();

    log_line("==== nextfeed feed list stage exiting cleanly ====");
    return 0;
}
'''

(ROOT / "src/main.cpp").write_text(main_cpp, encoding="utf-8")
print("Feed list stage written.")
