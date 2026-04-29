/*
 * NextFeed — RSS/Atom reader for NextUI
 * Apostrophe-based UI with config-backed feed loading.
 */

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include <string.h>

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

#define MAX_FEEDS    64
#define MAX_ARTICLES 32
#define MAX_LABEL    256
#define MAX_URL      512
#define MAX_LINE     1024

/* -----------------------------------------------------------------------
 * Data structures
 * ----------------------------------------------------------------------- */

typedef struct {
    char label[MAX_LABEL];
    char url[MAX_URL];
} feed_t;

typedef struct {
    char title[MAX_LABEL];
    char summary[MAX_URL];
} article_t;

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */

static feed_t    g_feeds[MAX_FEEDS];
static int       g_feed_count = 0;

static article_t g_articles[MAX_ARTICLES];
static int       g_article_count = 0;

/* -----------------------------------------------------------------------
 * String helpers
 * ----------------------------------------------------------------------- */

static void trim_inplace(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

/* -----------------------------------------------------------------------
 * Feed loading
 * ----------------------------------------------------------------------- */

static int load_feeds(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        ap_log("Could not open feeds file: %s", path);
        return 0;
    }

    char line[MAX_LINE];
    g_feed_count = 0;

    while (fgets(line, sizeof(line), f) && g_feed_count < MAX_FEEDS) {
        trim_inplace(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;

        char *sep = strchr(line, '|');
        if (sep) {
            *sep = '\0';
            strncpy(g_feeds[g_feed_count].label, line, MAX_LABEL - 1);
            strncpy(g_feeds[g_feed_count].url, sep + 1, MAX_URL - 1);
            trim_inplace(g_feeds[g_feed_count].label);
            trim_inplace(g_feeds[g_feed_count].url);
        } else {
            strncpy(g_feeds[g_feed_count].label, line, MAX_LABEL - 1);
            g_feeds[g_feed_count].url[0] = '\0';
        }

        if (g_feeds[g_feed_count].label[0] != '\0') {
            ap_log("  Feed %d: %s [%s]", g_feed_count,
                   g_feeds[g_feed_count].label,
                   g_feeds[g_feed_count].url[0] ? g_feeds[g_feed_count].url : "no url");
            g_feed_count++;
        }
    }

    fclose(f);
    ap_log("Loaded %d feed(s) from %s", g_feed_count, path);
    return g_feed_count;
}

static const char *resolve_feeds_path(void) {
    static char path[1024];

    const char *config_dir = getenv("NEXTFEED_CONFIG_DIR");
    if (config_dir) {
        snprintf(path, sizeof(path), "%s/feeds.txt", config_dir);
        if (access(path, R_OK) == 0) {
            ap_log("Using config feeds: %s", path);
            return path;
        }
    }

    const char *pak_dir = getenv("NEXTFEED_PAK_DIR");
    if (pak_dir) {
        snprintf(path, sizeof(path), "%s/assets/feeds/default_feeds.txt", pak_dir);
        if (access(path, R_OK) == 0) {
            ap_log("Using bundled feeds: %s", path);
            return path;
        }
    }

    snprintf(path, sizeof(path), "assets/feeds/default_feeds.txt");
    ap_log("Using local dev feeds: %s", path);
    return path;
}

/* -----------------------------------------------------------------------
 * Placeholder article generation
 * ----------------------------------------------------------------------- */

static void make_placeholder_articles(const feed_t *feed) {
    g_article_count = 0;
    for (int i = 0; i < 5 && g_article_count < MAX_ARTICLES; i++) {
        snprintf(g_articles[i].title, MAX_LABEL,
                 "%s - Article %d", feed->label, i + 1);
        snprintf(g_articles[i].summary, MAX_URL,
                 "Placeholder content for \"%s\" article %d.\n\n"
                 "This will be replaced with real fetched content once HTTP "
                 "and XML parsing are implemented.\n\n"
                 "Feed URL: %s",
                 feed->label, i + 1,
                 feed->url[0] ? feed->url : "(none)");
        g_article_count++;
    }
}

/* -----------------------------------------------------------------------
 * Screens
 * ----------------------------------------------------------------------- */

static int show_article_detail(int article_idx) {
    ap_detail_section sections[] = {
        {.type        = AP_SECTION_DESCRIPTION,.title       = "Summary",.description = g_articles[article_idx].summary,
        },
    };

    ap_footer_item footer[] = {
        {.button = AP_BTN_B,.label = "BACK" },
    };

    ap_detail_opts opts = {.title         = g_articles[article_idx].title,.sections      = sections,.section_count = 1,.footer        = footer,.footer_count  = 1,
    };

    ap_detail_result result;
    ap_detail_screen(&opts, &result);

    return 0;
}

static int show_article_list(int feed_idx) {
    make_placeholder_articles(&g_feeds[feed_idx]);

    ap_list_item items[MAX_ARTICLES];
    for (int i = 0; i < g_article_count; i++) {
        memset(&items[i], 0, sizeof(items[i]));
        items[i].label = g_articles[i].title;
        items[i].metadata = NULL;
    }

    ap_footer_item footer[] = {
        {.button = AP_BTN_B,.label = "BACK" },
        {.button = AP_BTN_A,.label = "READ",.is_confirm = true },
    };

    while (1) {
        ap_list_opts opts = ap_list_default_opts(
            g_feeds[feed_idx].label, items, g_article_count);
        opts.footer       = footer;
        opts.footer_count = 2;

        ap_list_result result;
        int rc = ap_list(&opts, &result);

        if (rc != AP_OK || result.selected_index < 0) {
            return 0;
        }

        show_article_detail(result.selected_index);
    }
}

static int show_feed_list(void) {
    ap_list_item items[MAX_FEEDS];
    for (int i = 0; i < g_feed_count; i++) {
        memset(&items[i], 0, sizeof(items[i]));
        items[i].label = g_feeds[i].label;
        items[i].metadata = NULL;
    }

    ap_footer_item footer[] = {
        {.button = AP_BTN_B,.label = "QUIT" },
        {.button = AP_BTN_A,.label = "OPEN",.is_confirm = true },
    };

    while (1) {
        ap_list_opts opts = ap_list_default_opts("NextFeed", items, g_feed_count);
        opts.footer       = footer;
        opts.footer_count = 2;

        ap_list_result result;
        int rc = ap_list(&opts, &result);

        if (rc != AP_OK || result.selected_index < 0) {
            return 0;
        }

        show_article_list(result.selected_index);
    }
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    ap_config cfg = {.window_title = "NextFeed",.log_path     = ap_resolve_log_path("nextfeed"),.is_nextui    = AP_PLATFORM_IS_DEVICE,
    };
    if (ap_init(&cfg) != AP_OK) {
        fprintf(stderr, "Failed to initialise Apostrophe\n");
        return 1;
    }
    ap_log("=== NextFeed starting ===");

    const char *feeds_path = resolve_feeds_path();
    load_feeds(feeds_path);

    if (g_feed_count == 0) {
        ap_log("WARNING: No feeds loaded.");
        ap_footer_item err_footer[] = {
            {.button = AP_BTN_B,.label = "QUIT" },
        };
        ap_message_opts msg = {.message      = "No feeds configured.\nCheck feeds.txt in config directory.",.image_path   = NULL,.footer       = err_footer,.footer_count = 1,
        };
        ap_confirm_result cr;
        ap_confirmation(&msg, &cr);
    } else {
        show_feed_list();
    }

    ap_log("=== NextFeed shutting down ===");
    ap_quit();
    return 0;
}