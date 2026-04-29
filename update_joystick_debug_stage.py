from pathlib import Path

ROOT = Path("/Users/htpc/Projects/nextui-rss-reader")

main_cpp = r'''#include <SDL.h>
#include <SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <string>

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

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    log_line("==== nextfeed joystick debug stage starting ====");

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

    int joystick_count = SDL_NumJoysticks();
    log_line("SDL_NumJoysticks=" + std::to_string(joystick_count));

    SDL_Joystick* joystick = nullptr;
    if (joystick_count > 0) {
        joystick = SDL_JoystickOpen(0);
        if (joystick) {
            const char* name = SDL_JoystickName(joystick);
            log_line(std::string("Joystick 0 opened: ") + (name ? name : "(unknown)"));
            log_line("Input backend: joystick");
        } else {
            log_line(std::string("SDL_JoystickOpen failed: ") + SDL_GetError());
        }
    } else {
        log_line("No joysticks detected");
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
    TTF_Font* body_font = TTF_OpenFont(font_path, 24);

    if (!title_font || !body_font) {
        log_line("TTF_OpenFont failed");
        log_line(TTF_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        if (joystick) SDL_JoystickClose(joystick);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    std::string status1 = "Joystick debug";
    std::string status2 = joystick ? "Joystick opened" : "No joystick opened";
    std::string last_event = "No joystick input yet";

    bool running = true;
    Uint32 start_ticks = SDL_GetTicks();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            char buf[256];

            switch (event.type) {
                case SDL_QUIT:
                    last_event = "SDL_QUIT";
                    log_line(last_event);
                    running = false;
                    break;

                case SDL_JOYBUTTONDOWN:
                    std::snprintf(buf, sizeof(buf), "JOYBUTTONDOWN button=%d state=%d",
                                  event.jbutton.button, event.jbutton.state);
                    last_event = buf;
                    log_line(last_event);

                    // temporary quit guesses: guide/menu/home commonly end up on higher button ids
                    if (event.jbutton.button == 10 || event.jbutton.button == 11 || event.jbutton.button == 12) {
                        log_line("Quiting on candidate menu/guide/home button");
                        running = false;
                    }
                    break;

                case SDL_JOYBUTTONUP:
                    std::snprintf(buf, sizeof(buf), "JOYBUTTONUP button=%d state=%d",
                                  event.jbutton.button, event.jbutton.state);
                    last_event = buf;
                    log_line(last_event);
                    break;

                case SDL_JOYHATMOTION:
                    std::snprintf(buf, sizeof(buf), "JOYHATMOTION hat=%d value=%d",
                                  event.jhat.hat, event.jhat.value);
                    last_event = buf;
                    log_line(last_event);
                    break;

                case SDL_JOYAXISMOTION:
                    if (event.jaxis.value > 16000 || event.jaxis.value < -16000) {
                        std::snprintf(buf, sizeof(buf), "JOYAXISMOTION axis=%d value=%d",
                                      event.jaxis.axis, event.jaxis.value);
                        last_event = buf;
                        log_line(last_event);
                    }
                    break;

                case SDL_JOYDEVICEADDED:
                    std::snprintf(buf, sizeof(buf), "JOYDEVICEADDED which=%d", event.jdevice.which);
                    last_event = buf;
                    log_line(last_event);
                    break;

                case SDL_JOYDEVICEREMOVED:
                    std::snprintf(buf, sizeof(buf), "JOYDEVICEREMOVED which=%d", event.jdevice.which);
                    last_event = buf;
                    log_line(last_event);
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

        SDL_Texture* t1 = render_text(renderer, title_font, status1, title_color, &w1, &h1);
        SDL_Texture* t2 = render_text(renderer, body_font, status2, body_color, &w2, &h2);
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
            log_line("Timeout exit");
            running = false;
        }

        SDL_Delay(16);
    }

    if (joystick) SDL_JoystickClose(joystick);
    TTF_CloseFont(title_font);
    TTF_CloseFont(body_font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    log_line("==== nextfeed joystick debug stage exiting cleanly ====");
    return 0;
}
'''

(ROOT / "src/main.cpp").write_text(main_cpp, encoding="utf-8")
print("Joystick debug stage written.")
