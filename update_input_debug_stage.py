from pathlib import Path

ROOT = Path("/Users/htpc/Projects/nextui-rss-reader")

main_cpp = r'''#include <SDL.h>
#include <SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <string>

static void log_line(const char* message) {
    const char* log_file = std::getenv("NEXTFEED_LOG_FILE");
    if (!log_file || !*log_file) return;
    FILE* f = std::fopen(log_file, "a");
    if (!f) return;
    std::fprintf(f, "%s\n", message);
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

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    log_line("==== nextfeed input debug stage starting ====");

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
        1024,
        768,
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
    TTF_Font* title_font = TTF_OpenFont(font_path, 40);
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

    std::string last_event = "No input yet";
    bool running = true;
    Uint32 start_ticks = SDL_GetTicks();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            char buf[256];

            switch (event.type) {
                case SDL_QUIT:
                    last_event = "SDL_QUIT";
                    running = false;
                    break;

                case SDL_KEYDOWN:
                    std::snprintf(buf, sizeof(buf), "KEYDOWN keycode=%d scancode=%d",
                                  event.key.keysym.sym, event.key.keysym.scancode);
                    last_event = buf;
                    log_line(buf);
                    break;

                case SDL_KEYUP:
                    std::snprintf(buf, sizeof(buf), "KEYUP keycode=%d scancode=%d",
                                  event.key.keysym.sym, event.key.keysym.scancode);
                    last_event = buf;
                    log_line(buf);
                    break;

                case SDL_CONTROLLERBUTTONDOWN:
                    std::snprintf(buf, sizeof(buf), "CONTROLLER DOWN button=%d",
                                  event.cbutton.button);
                    last_event = buf;
                    log_line(buf);
                    break;

                case SDL_CONTROLLERBUTTONUP:
                    std::snprintf(buf, sizeof(buf), "CONTROLLER UP button=%d",
                                  event.cbutton.button);
                    last_event = buf;
                    log_line(buf);
                    break;

                case SDL_JOYBUTTONDOWN:
                    std::snprintf(buf, sizeof(buf), "JOY DOWN button=%d",
                                  event.jbutton.button);
                    last_event = buf;
                    log_line(buf);
                    break;

                case SDL_JOYBUTTONUP:
                    std::snprintf(buf, sizeof(buf), "JOY UP button=%d",
                                  event.jbutton.button);
                    last_event = buf;
                    log_line(buf);
                    break;

                case SDL_JOYHATMOTION:
                    std::snprintf(buf, sizeof(buf), "JOY HAT value=%d",
                                  event.jhat.value);
                    last_event = buf;
                    log_line(buf);
                    break;

                case SDL_JOYAXISMOTION:
                    if (event.jaxis.value > 16000 || event.jaxis.value < -16000) {
                        std::snprintf(buf, sizeof(buf), "JOY AXIS axis=%d value=%d",
                                      event.jaxis.axis, event.jaxis.value);
                        last_event = buf;
                        log_line(buf);
                    }
                    break;

                default:
                    break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
        SDL_RenderClear(renderer);

        SDL_Color title_color{240, 240, 240, 255};
        SDL_Color body_color{180, 180, 200, 255};

        int w1 = 0, h1 = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0;

        SDL_Texture* t1 = render_text(renderer, title_font, "NextFeed Input Test", title_color, &w1, &h1);
        SDL_Texture* t2 = render_text(renderer, body_font, "Press buttons and watch the log / screen", body_color, &w2, &h2);
        SDL_Texture* t3 = render_text(renderer, body_font, last_event, body_color, &w3, &h3);

        if (t1) {
            SDL_Rect r{40, 40, w1, h1};
            SDL_RenderCopy(renderer, t1, nullptr, &r);
            SDL_DestroyTexture(t1);
        }

        if (t2) {
            SDL_Rect r{40, 120, w2, h2};
            SDL_RenderCopy(renderer, t2, nullptr, &r);
            SDL_DestroyTexture(t2);
        }

        if (t3) {
            SDL_Rect r{40, 200, w3, h3};
            SDL_RenderCopy(renderer, t3, nullptr, &r);
            SDL_DestroyTexture(t3);
        }

        SDL_RenderPresent(renderer);

        if (SDL_GetTicks() - start_ticks > 15000) {
            running = false;
        }

        SDL_Delay(16);
    }

    TTF_CloseFont(title_font);
    TTF_CloseFont(body_font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    log_line("==== nextfeed input debug stage exiting cleanly ====");
    return 0;
}
'''

(ROOT / "src/main.cpp").write_text(main_cpp, encoding="utf-8")

print("Input debug stage written.")
print("Next steps:")
print("  cd /Users/htpc/Projects/nextui-rss-reader")
print("  python3 update_input_debug_stage.py")
print("  make tg5040-docker")
