// src/main.cpp — NextFeed for TrimUI (tg5040)
// Config-backed feed loading + refresh stub

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

struct Feed {
    std::string label;
    std::string url;
};

struct Article {
    std::string title;
    std::string summary;
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static const int SCREEN_W = 1024;
static const int SCREEN_H = 768;

static SDL_Window*   g_window   = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static TTF_Font*     g_font     = nullptr;
static SDL_Joystick* g_joy      = nullptr;

static std::vector<Feed>    g_feeds;
static std::vector<Article> g_articles;
static std::string          g_detail_text;

static int g_feed_sel    = 0;
static int g_article_sel = 0;
static int g_scroll_y    = 0;

enum Screen { SCREEN_FEEDS, SCREEN_ARTICLES, SCREEN_DETAIL };
static Screen g_screen = SCREEN_FEEDS;

// Status bar message (shown briefly on feed list)
static std::string g_status_msg;
static Uint32      g_status_expire = 0;

static const char* LOG_TAG = "NextFeed";

// ---------------------------------------------------------------------------
// Logging helper
// ---------------------------------------------------------------------------

static FILE* g_logfile = nullptr;

static void log_msg(const char* fmt,...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", LOG_TAG);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    if (g_logfile) {
        va_start(ap, fmt);
        fprintf(g_logfile, "[%s] ", LOG_TAG);
        vfprintf(g_logfile, fmt, ap);
        fprintf(g_logfile, "\n");
        fflush(g_logfile);
        va_end(ap);
    }
}

// ---------------------------------------------------------------------------
// Config-backed feed loading
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<Feed> load_feeds_from_file(const std::string& path) {
    std::vector<Feed> feeds;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        log_msg("Could not open feeds file: %s", path.c_str());
        return feeds;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        Feed f;
        size_t sep = line.find('|');
        if (sep != std::string::npos) {
            f.label = trim(line.substr(0, sep));
            f.url   = trim(line.substr(sep + 1));
        } else {
            f.label = line;
            f.url   = "";
        }
        if (!f.label.empty()) {
            feeds.push_back(f);
        }
    }
    log_msg("Loaded %zu feed(s) from %s", feeds.size(), path.c_str());
    return feeds;
}

static std::string resolve_feeds_path() {
    const char* config_dir = std::getenv("NEXTFEED_CONFIG_DIR");
    if (config_dir) {
        std::string p = std::string(config_dir) + "/feeds.txt";
        std::ifstream test(p);
        if (test.good()) {
            log_msg("Using config feeds: %s", p.c_str());
            return p;
        }
    }

    const char* pak_dir = std::getenv("NEXTFEED_PAK_DIR");
    if (pak_dir) {
        std::string p = std::string(pak_dir) + "/assets/feeds/default_feeds.txt";
        std::ifstream test(p);
        if (test.good()) {
            log_msg("Using bundled feeds: %s", p.c_str());
            return p;
        }
    }

    std::string p = "assets/feeds/default_feeds.txt";
    log_msg("Using local dev feeds: %s", p.c_str());
    return p;
}

static std::vector<Article> make_placeholder_articles(const Feed& feed) {
    std::vector<Article> arts;
    for (int i = 1; i <= 5; i++) {
        Article a;
        a.title   = feed.label + " — Article " + std::to_string(i);
        a.summary = "Placeholder content for \"" + feed.label + "\" article " +
                    std::to_string(i) + ".\n\n"
                    "This will be replaced with real fetched content once HTTP "
                    "and XML parsing are implemented.\n\n"
                    "Feed URL: " + (feed.url.empty() ? "(none)" : feed.url);
        arts.push_back(a);
    }
    return arts;
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------

static void render_text(const char* text, int x, int y, SDL_Color color) {
    if (!text || !text[0]) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(g_font, text, color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(g_renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static int render_wrapped_text(const char* text, int x, int y, int max_w,
                               SDL_Color color) {
    if (!text || !text[0]) return 0;
    int line_h = TTF_FontLineSkip(g_font);
    int cur_y = y;

    std::istringstream stream(text);
    std::string paragraph;
    while (std::getline(stream, paragraph, '\n')) {
        if (paragraph.empty()) {
            cur_y += line_h;
            continue;
        }
        std::istringstream words(paragraph);
        std::string word;
        std::string line;
        while (words >> word) {
            std::string test = line.empty() ? word : line + " " + word;
            int tw = 0, th = 0;
            TTF_SizeUTF8(g_font, test.c_str(), &tw, &th);
            if (tw > max_w && !line.empty()) {
                render_text(line.c_str(), x, cur_y, color);
                cur_y += line_h;
                line = word;
            } else {
                line = test;
            }
        }
        if (!line.empty()) {
            render_text(line.c_str(), x, cur_y, color);
            cur_y += line_h;
        }
    }
    return cur_y - y;
}

// ---------------------------------------------------------------------------
// Screen renderers
// ---------------------------------------------------------------------------

static void render_feed_list() {
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color gray   = {160, 160, 160, 255};

    render_text("NextFeed — Feeds", 20, 10, white);

    if (g_feeds.empty()) {
        render_text("No feeds configured.", 30, 60, gray);
        render_text("Check $NEXTFEED_CONFIG_DIR/feeds.txt", 30, 90, gray);
        return;
    }

    int y = 50;
    int line_h = TTF_FontLineSkip(g_font) + 4;
    for (int i = 0; i < (int)g_feeds.size(); i++) {
        SDL_Color c = (i == g_feed_sel) ? yellow : white;
        const char* marker = (i == g_feed_sel) ? "> " : "  ";
        std::string display = std::string(marker) + g_feeds[i].label;
        render_text(display.c_str(), 20, y, c);
        y += line_h;
    }

    // Status message (timed)
    if (!g_status_msg.empty() && SDL_GetTicks() < g_status_expire) {
        SDL_Color cyan = {0, 220, 255, 255};
        render_text(g_status_msg.c_str(), 20, SCREEN_H - 60, cyan);
    } else {
        g_status_msg.clear();
    }

    render_text("[A] Open   [X] Refresh   [B/Start] Exit", 20, SCREEN_H - 30, gray);
}

static void render_article_list() {
    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color gray   = {160, 160, 160, 255};

    std::string title = "NextFeed — " + g_feeds[g_feed_sel].label;
    render_text(title.c_str(), 20, 10, white);

    if (g_articles.empty()) {
        render_text("No articles.", 30, 60, gray);
        return;
    }

    int y = 50;
    int line_h = TTF_FontLineSkip(g_font) + 4;
    for (int i = 0; i < (int)g_articles.size(); i++) {
        SDL_Color c = (i == g_article_sel) ? yellow : white;
        const char* marker = (i == g_article_sel) ? "> " : "  ";
        std::string display = std::string(marker) + g_articles[i].title;
        render_text(display.c_str(), 20, y, c);
        y += line_h;
    }

    render_text("[A] Read   [B] Back   [Start] Exit", 20, SCREEN_H - 30, gray);
}

static void render_article_detail() {
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {160, 160, 160, 255};

    render_text(g_articles[g_article_sel].title.c_str(), 20, 10, white);

    int content_y = 50 + g_scroll_y;
    render_wrapped_text(g_detail_text.c_str(), 20, content_y,
                        SCREEN_W - 40, white);

    render_text("[Up/Down] Scroll   [B] Back   [Start] Exit",
                20, SCREEN_H - 30, gray);
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

static const int HAT_UP    = 1;
static const int HAT_DOWN  = 4;

static const int BTN_B     = 0;
static const int BTN_A     = 1;
static const int BTN_X     = 3;
static const int BTN_START = 7;

static bool handle_input(bool& running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            running = false;
            return true;
        }

        if (e.type == SDL_JOYHATMOTION) {
            int hat = e.jhat.value;

            if (g_screen == SCREEN_FEEDS) {
                if (hat == HAT_UP && g_feed_sel > 0) g_feed_sel--;
                if (hat == HAT_DOWN && g_feed_sel < (int)g_feeds.size() - 1) g_feed_sel++;
            }
            else if (g_screen == SCREEN_ARTICLES) {
                if (hat == HAT_UP && g_article_sel > 0) g_article_sel--;
                if (hat == HAT_DOWN && g_article_sel < (int)g_articles.size() - 1) g_article_sel++;
            }
            else if (g_screen == SCREEN_DETAIL) {
                int scroll_step = TTF_FontLineSkip(g_font) * 3;
                if (hat == HAT_UP)   g_scroll_y += scroll_step;
                if (hat == HAT_DOWN) g_scroll_y -= scroll_step;
                if (g_scroll_y > 0) g_scroll_y = 0;
            }
        }

        if (e.type == SDL_JOYBUTTONDOWN) {
            int btn = e.jbutton.button;

            if (btn == BTN_START) {
                running = false;
                return true;
            }

            if (g_screen == SCREEN_FEEDS) {
                if (btn == BTN_A && !g_feeds.empty()) {
                    g_articles = make_placeholder_articles(g_feeds[g_feed_sel]);
                    g_article_sel = 0;
                    g_screen = SCREEN_ARTICLES;
                }
                if (btn == BTN_X && !g_feeds.empty()) {
                    log_msg("Refresh requested for: %s [%s]",
                            g_feeds[g_feed_sel].label.c_str(),
                            g_feeds[g_feed_sel].url.empty() ? "no url" : g_feeds[g_feed_sel].url.c_str());
                    // TODO: HTTP fetch + XML parse goes here
                    g_status_msg = "Refreshing " + g_feeds[g_feed_sel].label + "...";
                    g_status_expire = SDL_GetTicks() + 1500;
                    log_msg("Refresh stub complete (placeholder only)");
                }
                if (btn == BTN_B) {
                    running = false;
                    return true;
                }
            }
            else if (g_screen == SCREEN_ARTICLES) {
                if (btn == BTN_A && !g_articles.empty()) {
                    g_detail_text = g_articles[g_article_sel].summary;
                    g_scroll_y = 0;
                    g_screen = SCREEN_DETAIL;
                }
                if (btn == BTN_B) {
                    g_screen = SCREEN_FEEDS;
                }
            }
            else if (g_screen == SCREEN_DETAIL) {
                if (btn == BTN_B) {
                    g_screen = SCREEN_ARTICLES;
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------

static bool init() {
    const char* log_path = std::getenv("NEXTFEED_LOG_FILE");
    if (log_path) {
        g_logfile = fopen(log_path, "a");
    }

    log_msg("=== NextFeed starting ===");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        log_msg("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (TTF_Init() < 0) {
        log_msg("TTF_Init failed: %s", TTF_GetError());
        return false;
    }

    g_window = SDL_CreateWindow("NextFeed",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    if (!g_window) {
        log_msg("CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        log_msg("CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    // Font: try bundled, then system fallback
    const char* pak_dir = std::getenv("NEXTFEED_PAK_DIR");
    std::string font_path;
    if (pak_dir) {
        font_path = std::string(pak_dir) + "/assets/fonts/font1.ttf";
    }
    g_font = nullptr;
    if (!font_path.empty()) {
        g_font = TTF_OpenFont(font_path.c_str(), 20);
    }
    if (!g_font) {
        g_font = TTF_OpenFont("/mnt/SDCARD/.system/res/font1.ttf", 20);
    }
    if (!g_font) {
        log_msg("Could not open any font: %s", TTF_GetError());
        return false;
    }

    // Joystick
    int num_joy = SDL_NumJoysticks();
    log_msg("Joysticks found: %d", num_joy);
    if (num_joy > 0) {
        g_joy = SDL_JoystickOpen(0);
        if (g_joy) {
            log_msg("Joystick 0 opened: %s", SDL_JoystickName(g_joy));
        }
    }

    // Load feeds from config
    std::string feeds_path = resolve_feeds_path();
    g_feeds = load_feeds_from_file(feeds_path);

    if (g_feeds.empty()) {
        log_msg("WARNING: No feeds loaded. UI will show empty state.");
    } else {
        for (size_t i = 0; i < g_feeds.size(); i++) {
            log_msg("  Feed %zu: %s [%s]", i, g_feeds[i].label.c_str(),
                    g_feeds[i].url.empty() ? "no url" : g_feeds[i].url.c_str());
        }
    }

    return true;
}

static void shutdown() {
    log_msg("=== NextFeed shutting down ===");
    if (g_joy)      SDL_JoystickClose(g_joy);
    if (g_font)     TTF_CloseFont(g_font);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window)   SDL_DestroyWindow(g_window);
    TTF_Quit();
    SDL_Quit();
    if (g_logfile)  fclose(g_logfile);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    if (!init()) {
        shutdown();
        return 1;
    }

    bool running = true;
    while (running) {
        handle_input(running);

        SDL_SetRenderDrawColor(g_renderer, 20, 20, 30, 255);
        SDL_RenderClear(g_renderer);

        switch (g_screen) {
            case SCREEN_FEEDS:    render_feed_list();     break;
            case SCREEN_ARTICLES: render_article_list();  break;
            case SCREEN_DETAIL:   render_article_detail(); break;
        }

        SDL_RenderPresent(g_renderer);
        SDL_Delay(16);
    }

    shutdown();
    return 0;
}