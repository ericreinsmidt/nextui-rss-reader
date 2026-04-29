from pathlib import Path

ROOT = Path("/Users/htpc/Projects/nextui-rss-reader")

demo_content = """FEED|Hacker News
ARTICLE|HN Article 1
BODY|This is a placeholder article body.
BODY|It simulates a summary or content view.
BODY|NextFeed will eventually render cached feed items.
BODY|Line 4 of the article body.
BODY|Line 5 of the article body.
BODY|Line 6 of the article body.
BODY|Line 7 of the article body.
BODY|Line 8 of the article body.

ARTICLE|HN Article 2
BODY|Another article from the Hacker News feed.
BODY|This view will later show parsed article summaries.
BODY|Scrolling should work with the D-pad.
BODY|More placeholder text here.
BODY|Even more placeholder text here.

ARTICLE|HN Article 3
BODY|Third Hacker News article placeholder.
BODY|The goal is to validate navigation and layout.

FEED|Planet GNOME
ARTICLE|GNOME Post 1
BODY|Planet GNOME placeholder article content.
BODY|This will eventually be normalized from RSS or Atom.

ARTICLE|GNOME Post 2
BODY|Second GNOME placeholder article.
BODY|More lines here for scrolling.
BODY|Another line.
BODY|Another line.
BODY|Another line.
BODY|Another line.

ARTICLE|GNOME Post 3
BODY|Third GNOME placeholder article.

FEED|LWN Headlines
ARTICLE|LWN Story 1
BODY|LWN placeholder article body line 1.
BODY|LWN placeholder article body line 2.
BODY|LWN placeholder article body line 3.

ARTICLE|LWN Story 2
BODY|Second LWN article placeholder.

ARTICLE|LWN Story 3
BODY|Third LWN article placeholder.
"""

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
    "ASSET_DATA_DIR := $(PAK_DIR)/assets/data",
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
    '\tmkdir -p $(ASSET_FEEDS_DIR) $(ASSET_THEMES_DIR) $(ASSET_DATA_DIR) $(BIN_DIR)',
    '\tcp $(ROOT_DIR)/assets/feeds/default_feeds.txt $(ASSET_FEEDS_DIR)/default_feeds.txt',
    '\tcp $(ROOT_DIR)/assets/themes/default.theme $(ASSET_THEMES_DIR)/default.theme',
    '\tcp $(ROOT_DIR)/assets/data/demo_content.txt $(ASSET_DATA_DIR)/demo_content.txt',
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
    '\trm -f $(ASSET_DATA_DIR)/demo_content.txt',
    "",
])

main_cpp = r'''#include <SDL.h>
#include <SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

struct Article {
    std::string title;
    std::vector<std::string> body_lines;
};

struct Feed {
    std::string title;
    std::vector<Article> articles;
};

enum class Screen {
    FeedList,
    ArticleList,
    ArticleDetail
};

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

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

static std::vector<Feed> load_demo_content(const std::string& path) {
    std::vector<Feed> feeds;
    std::ifstream in(path);
    if (!in) {
        log_line("Failed to open demo content: " + path);
        return feeds;
    }

    Feed* current_feed = nullptr;
    Article* current_article = nullptr;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        if (starts_with(line, "FEED|")) {
            Feed feed;
            feed.title = line.substr(5);
            feeds.push_back(feed);
            current_feed = &feeds.back();
            current_article = nullptr;
        } else if (starts_with(line, "ARTICLE|")) {
            if (!current_feed) continue;
            Article article;
            article.title = line.substr(8);
            current_feed->articles.push_back(article);
            current_article = &current_feed->articles.back();
        } else if (starts_with(line, "BODY|")) {
            if (!current_article) continue;
            current_article->body_lines.push_back(line.substr(5));
        }
    }

    log_line("Loaded feeds: " + std::to_string(feeds.size()));
    return feeds;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    log_line("==== nextfeed file data stage starting ====");

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

    const char* pak_dir = std::getenv("NEXTFEED_PAK_DIR");
    std::string demo_path = pak_dir
        ? std::string(pak_dir) + "/assets/data/demo_content.txt"
        : "assets/data/demo_content.txt";

    std::vector<Feed> feeds = load_demo_content(demo_path);
    if (feeds.empty()) {
        feeds.push_back({"No feeds loaded", {{"No articles", {"Could not load demo content."}}}});
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
    TTF_Font* title_font = TTF_OpenFont(font_path, 36);
    TTF_Font* item_font = TTF_OpenFont(font_path, 24);
    TTF_Font* hint_font = TTF_OpenFont(font_path, 20);
    TTF_Font* body_font = TTF_OpenFont(font_path, 22);

    if (!title_font || !item_font || !hint_font || !body_font) {
        log_line("TTF_OpenFont failed");
        log_line(TTF_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        if (joystick) SDL_JoystickClose(joystick);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    Screen screen = Screen::FeedList;
    int selected_feed = 0;
    int selected_article = 0;
    int article_scroll = 0;

    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            if (event.type == SDL_JOYHATMOTION && event.jhat.hat == 0) {
                if (event.jhat.value == 1) {
                    if (screen == Screen::FeedList) {
                        selected_feed--;
                        if (selected_feed < 0) selected_feed = static_cast<int>(feeds.size()) - 1;
                    } else if (screen == Screen::ArticleList) {
                        selected_article--;
                        if (selected_article < 0) {
                            selected_article = static_cast<int>(feeds[selected_feed].articles.size()) - 1;
                        }
                    } else if (screen == Screen::ArticleDetail) {
                        article_scroll--;
                        if (article_scroll < 0) article_scroll = 0;
                    }
                } else if (event.jhat.value == 4) {
                    if (screen == Screen::FeedList) {
                        selected_feed++;
                        if (selected_feed >= static_cast<int>(feeds.size())) selected_feed = 0;
                    } else if (screen == Screen::ArticleList) {
                        selected_article++;
                        if (selected_article >= static_cast<int>(feeds[selected_feed].articles.size())) {
                            selected_article = 0;
                        }
                    } else if (screen == Screen::ArticleDetail) {
                        const auto& lines = feeds[selected_feed].articles[selected_article].body_lines;
                        int max_scroll = static_cast<int>(lines.size()) - 10;
                        if (max_scroll < 0) max_scroll = 0;
                        article_scroll++;
                        if (article_scroll > max_scroll) article_scroll = max_scroll;
                    }
                }
            }

            if (event.type == SDL_JOYBUTTONDOWN) {
                if (event.jbutton.button == 1) {
                    if (screen == Screen::FeedList) {
                        screen = Screen::ArticleList;
                        selected_article = 0;
                    } else if (screen == Screen::ArticleList) {
                        screen = Screen::ArticleDetail;
                        article_scroll = 0;
                    }
                } else if (event.jbutton.button == 0) {
                    if (screen == Screen::ArticleDetail) {
                        screen = Screen::ArticleList;
                    } else if (screen == Screen::ArticleList) {
                        screen = Screen::FeedList;
                    } else {
                        running = false;
                    }
                } else if (event.jbutton.button == 7) {
                    running = false;
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
        SDL_RenderClear(renderer);

        SDL_Color title_color{240, 240, 240, 255};
        SDL_Color normal_color{180, 180, 200, 255};
        SDL_Color selected_color{255, 220, 120, 255};
        SDL_Color hint_color{120, 180, 220, 255};

        if (screen == Screen::FeedList) {
            draw_text(renderer, title_font, "NextFeed", title_color, 40, 40);
            draw_text(renderer, hint_font, "Feeds   D-pad: move   A: open   B: exit   Start: exit", hint_color, 40, 95);

            for (size_t i = 0; i < feeds.size(); ++i) {
                SDL_Color color = (static_cast<int>(i) == selected_feed) ? selected_color : normal_color;

                if (static_cast<int>(i) == selected_feed) {
                    SDL_SetRenderDrawColor(renderer, 50, 50, 70, 255);
                    SDL_Rect highlight{30, 145 + static_cast<int>(i) * 60, 760, 42};
                    SDL_RenderFillRect(renderer, &highlight);
                }

                draw_text(renderer, item_font, feeds[i].title, color, 50, 153 + static_cast<int>(i) * 60);
            }
        } else if (screen == Screen::ArticleList) {
            draw_text(renderer, title_font, feeds[selected_feed].title, title_color, 40, 40);
            draw_text(renderer, hint_font, "Articles   D-pad: move   A: open   B: back   Start: exit", hint_color, 40, 95);

            const auto& articles = feeds[selected_feed].articles;
            for (size_t i = 0; i < articles.size(); ++i) {
                SDL_Color color = (static_cast<int>(i) == selected_article) ? selected_color : normal_color;

                if (static_cast<int>(i) == selected_article) {
                    SDL_SetRenderDrawColor(renderer, 50, 50, 70, 255);
                    SDL_Rect highlight{30, 145 + static_cast<int>(i) * 60, 920, 42};
                    SDL_RenderFillRect(renderer, &highlight);
                }

                draw_text(renderer, item_font, articles[i].title, color, 50, 153 + static_cast<int>(i) * 60);
            }
        } else {
            const auto& article = feeds[selected_feed].articles[selected_article];

            draw_text(renderer, title_font, article.title, title_color, 40, 30);
            draw_text(renderer, hint_font, "Article   Up/Down: scroll   B: back   Start: exit", hint_color, 40, 78);

            int y = 130;
            const int line_height = 32;
            for (size_t i = static_cast<size_t>(article_scroll); i < article.body_lines.size(); ++i) {
                if (y > 720) break;
                draw_text(renderer, body_font, article.body_lines[i], normal_color, 40, y);
                y += line_height;
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    TTF_CloseFont(title_font);
    TTF_CloseFont(item_font);
    TTF_CloseFont(hint_font);
    TTF_CloseFont(body_font);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (joystick) SDL_JoystickClose(joystick);
    TTF_Quit();
    SDL_Quit();

    log_line("==== nextfeed file data stage exiting cleanly ====");
    return 0;
}
'''

(ROOT / "assets/data").mkdir(parents=True, exist_ok=True)
(ROOT / "assets/data/demo_content.txt").write_text(demo_content, encoding="utf-8")
(ROOT / "ports/tg5040/pak/assets/data").mkdir(parents=True, exist_ok=True)
(ROOT / "ports/tg5040/pak/assets/data/demo_content.txt").write_text(demo_content, encoding="utf-8")
(ROOT / "ports/tg5040/Makefile").write_text(makefile_text, encoding="utf-8")
(ROOT / "src/main.cpp").write_text(main_cpp, encoding="utf-8")

print("File data stage written.")
