/*
 * NextFeed — RSS/Atom reader for NextUI
 * Apostrophe UI + libcurl + RSS/Atom parsing.
 * Features: loading indicator, refresh, feed management (add/edit/delete).
 */

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include <string.h>
#include <curl/curl.h>

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

#define MAX_FEEDS    64
#define MAX_ARTICLES 32
#define MAX_TITLE    512
#define MAX_URL      512
#define MAX_DESC     4096
#define MAX_LABEL    256
#define MAX_LINE     1024
#define MAX_DATE     64
#define MAX_DOMAIN   128
#define MIN_USEFUL_DESC 50

/* -----------------------------------------------------------------------
 * Data structures
 * ----------------------------------------------------------------------- */

typedef struct {
    char label[MAX_LABEL];
    char url[MAX_URL];
} feed_t;

typedef struct {
    char title[MAX_TITLE];
    char link[MAX_URL];
    char description[MAX_DESC];
    char pub_date[MAX_DATE];
    char domain[MAX_DOMAIN];
} article_t;

typedef struct {
    char  *data;
    size_t size;
    size_t capacity;
} fetch_buf_t;

typedef struct {
    const char *url;
    fetch_buf_t buf;
    int         result;
} fetch_task_t;

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */

static feed_t    g_feeds[MAX_FEEDS];
static int       g_feed_count = 0;

static article_t g_articles[MAX_ARTICLES];
static int       g_article_count = 0;

static char      g_feeds_path[1024] = {0};

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

static void decode_xml_entities(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '&') {
            if (strncmp(r, "&amp;", 5) == 0)         { *w++ = '&'; r += 5; }
            else if (strncmp(r, "&lt;", 4) == 0)      { *w++ = '<'; r += 4; }
            else if (strncmp(r, "&gt;", 4) == 0)      { *w++ = '>'; r += 4; }
            else if (strncmp(r, "&quot;", 6) == 0)     { *w++ = '"'; r += 6; }
            else if (strncmp(r, "&apos;", 6) == 0)     { *w++ = '\''; r += 6; }
            else if (strncmp(r, "&#x27;", 6) == 0)    { *w++ = '\''; r += 6; }
            else if (strncmp(r, "&#39;", 5) == 0)     { *w++ = '\''; r += 5; }
            else if (strncmp(r, "&#34;", 5) == 0)     { *w++ = '"'; r += 5; }
            else if (strncmp(r, "&ldquo;", 7) == 0)   { *w++ = '"'; r += 7; }
            else if (strncmp(r, "&rdquo;", 7) == 0)   { *w++ = '"'; r += 7; }
            else if (strncmp(r, "&lsquo;", 7) == 0)   { *w++ = '\''; r += 7; }
            else if (strncmp(r, "&rsquo;", 7) == 0)   { *w++ = '\''; r += 7; }
            else if (strncmp(r, "&#8220;", 7) == 0)   { *w++ = '"'; r += 7; }
            else if (strncmp(r, "&#8221;", 7) == 0)   { *w++ = '"'; r += 7; }
            else if (strncmp(r, "&#8216;", 7) == 0)   { *w++ = '\''; r += 7; }
            else if (strncmp(r, "&#8217;", 7) == 0)   { *w++ = '\''; r += 7; }
            else if (strncmp(r, "&mdash;", 7) == 0)   { *w++ = '-'; *w++ = '-'; r += 7; }
            else if (strncmp(r, "&ndash;", 7) == 0)   { *w++ = '-'; r += 7; }
            else if (strncmp(r, "&#8212;", 7) == 0)   { *w++ = '-'; *w++ = '-'; r += 7; }
            else if (strncmp(r, "&#8211;", 7) == 0)   { *w++ = '-'; r += 7; }
            else if (strncmp(r, "&hellip;", 8) == 0)  { *w++ = '.'; *w++ = '.'; *w++ = '.'; r += 8; }
            else if (strncmp(r, "&#8230;", 7) == 0)   { *w++ = '.'; *w++ = '.'; *w++ = '.'; r += 7; }
            else if (strncmp(r, " ", 6) == 0)    { *w++ = ' '; r += 6; }
            else if (strncmp(r, "&#160;", 6) == 0)    { *w++ = ' '; r += 6; }
            else if (strncmp(r, "&copy;", 6) == 0)    { *w++ = '('; *w++ = 'c'; *w++ = ')'; r += 6; }
            else if (strncmp(r, "&reg;", 5) == 0)     { *w++ = '('; *w++ = 'R'; *w++ = ')'; r += 5; }
            else if (strncmp(r, "&trade;", 7) == 0)   { *w++ = 'T'; *w++ = 'M'; r += 7; }
            else { *w++ = *r++; }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static void strip_html_tags(char *s) {
    char *r = s, *w = s;
    int in_tag = 0;
    int in_cdata = 0;

    while (*r) {
        if (strncmp(r, "<![CDATA[", 9) == 0) {
            r += 9;
            in_cdata = 1;
            continue;
        }
        if (in_cdata && strncmp(r, "]]>", 3) == 0) {
            r += 3;
            in_cdata = 0;
            continue;
        }
        if (in_cdata) {
            if (*r == '<') { in_tag = 1; r++; continue; }
            if (*r == '>' && in_tag) { in_tag = 0; r++; continue; }
            if (!in_tag) *w++ = *r;
            r++;
            continue;
        }

        if (*r == '<') { in_tag = 1; r++; continue; }
        if (*r == '>' && in_tag) { in_tag = 0; r++; continue; }
        if (!in_tag) *w++ = *r;
        r++;
    }
    *w = '\0';
}

/* -----------------------------------------------------------------------
 * Domain extraction
 * ----------------------------------------------------------------------- */

static void extract_domain(const char *url, char *domain, size_t domain_size) {
    domain[0] = '\0';
    if (!url || !url[0]) return;

    const char *start = strstr(url, "://");
    if (start) {
        start += 3;
    } else {
        start = url;
    }

    if (strncmp(start, "www.", 4) == 0) {
        start += 4;
    }

    const char *end = strchr(start, '/');
    size_t len;
    if (end) {
        len = (size_t)(end - start);
    } else {
        len = strlen(start);
    }

    if (len >= domain_size) len = domain_size - 1;
    memcpy(domain, start, len);
    domain[len] = '\0';
}

/* -----------------------------------------------------------------------
 * Date parsing
 * ----------------------------------------------------------------------- */

static const char *month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static int month_from_name(const char *name) {
    for (int i = 0; i < 12; i++) {
        if (strncasecmp(name, month_names[i], 3) == 0) return i + 1;
    }
    return 0;
}

static void parse_rfc2822_date(const char *raw, char *out, size_t out_size) {
    out[0] = '\0';
    const char *p = strchr(raw, ',');
    if (p) {
        p++;
        while (*p == ' ') p++;
    } else {
        p = raw;
    }

    int day = 0;
    char mon_str[16] = {0};
    int year = 0;
    int hour = 0, min = 0;

    if (sscanf(p, "%d %15s %d %d:%d", &day, mon_str, &year, &hour, &min) >= 3) {
        int month = month_from_name(mon_str);
        if (month > 0 && day > 0 && year > 0) {
            if (hour >= 0 && min >= 0) {
                snprintf(out, out_size, "%s %d, %d %02d:%02d",
                         month_names[month - 1], day, year, hour, min);
            } else {
                snprintf(out, out_size, "%s %d, %d",
                         month_names[month - 1], day, year);
            }
        }
    }
}

static void parse_iso8601_date(const char *raw, char *out, size_t out_size) {
    out[0] = '\0';
    int year = 0, month = 0, day = 0, hour = 0, min = 0;

    if (sscanf(raw, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &min) >= 3) {
        if (month >= 1 && month <= 12 && day > 0 && year > 0) {
            if (hour >= 0 && min >= 0) {
                snprintf(out, out_size, "%s %d, %d %02d:%02d",
                         month_names[month - 1], day, year, hour, min);
            } else {
                snprintf(out, out_size, "%s %d, %d",
                         month_names[month - 1], day, year);
            }
        }
    }
}

static void parse_date(const char *raw, char *out, size_t out_size) {
    out[0] = '\0';
    if (!raw || !raw[0]) return;
    if (raw[0] >= '0' && raw[0] <= '9') {
        parse_iso8601_date(raw, out, out_size);
    } else {
        parse_rfc2822_date(raw, out, out_size);
    }
}

static int is_useful_description(const char *desc) {
    if (!desc || !desc[0]) return 0;
    if (strlen(desc) < MIN_USEFUL_DESC) return 0;
    if (strcasecmp(desc, "Comments") == 0) return 0;
    return 1;
}

/* -----------------------------------------------------------------------
 * Feed config loading and saving
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

static int save_feeds(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        ap_log("Could not write feeds file: %s", path);
        return -1;
    }

    fprintf(f, "# NextFeed — Feed Configuration\n");
    fprintf(f, "# Format: Label|URL (one per line)\n\n");

    for (int i = 0; i < g_feed_count; i++) {
        if (g_feeds[i].url[0]) {
            fprintf(f, "%s|%s\n", g_feeds[i].label, g_feeds[i].url);
        } else {
            fprintf(f, "%s\n", g_feeds[i].label);
        }
    }

    fclose(f);
    ap_log("Saved %d feed(s) to %s", g_feed_count, path);
    return 0;
}

static const char *resolve_feeds_path(void) {
    const char *config_dir = getenv("NEXTFEED_CONFIG_DIR");
    if (config_dir) {
        snprintf(g_feeds_path, sizeof(g_feeds_path), "%s/feeds.txt", config_dir);
        if (access(g_feeds_path, R_OK) == 0) {
            ap_log("Using config feeds: %s", g_feeds_path);
            return g_feeds_path;
        }
    }

    const char *pak_dir = getenv("NEXTFEED_PAK_DIR");
    if (pak_dir) {
        snprintf(g_feeds_path, sizeof(g_feeds_path), "%s/assets/feeds/default_feeds.txt", pak_dir);
        if (access(g_feeds_path, R_OK) == 0) {
            ap_log("Using bundled feeds: %s", g_feeds_path);
            return g_feeds_path;
        }
    }

    snprintf(g_feeds_path, sizeof(g_feeds_path), "assets/feeds/default_feeds.txt");
    ap_log("Using local dev feeds: %s", g_feeds_path);
    return g_feeds_path;
}

/* -----------------------------------------------------------------------
 * HTTP fetch via libcurl
 * ----------------------------------------------------------------------- */

static size_t fetch_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    fetch_buf_t *buf = (fetch_buf_t *)userdata;
    size_t bytes = size * nmemb;

    while (buf->size + bytes + 1 > buf->capacity) {
        size_t new_cap = buf->capacity * 2;
        if (new_cap == 0) new_cap = 8192;
        char *new_data = realloc(buf->data, new_cap);
        if (!new_data) return 0;
        buf->data = new_data;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->size, ptr, bytes);
    buf->size += bytes;
    buf->data[buf->size] = '\0';
    return bytes;
}

static int fetch_url(const char *url, fetch_buf_t *buf) {
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
        ap_log("fetch: curl_easy_init failed");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NextFeed/0.1");

    const char *ca = getenv("CURL_CA_BUNDLE");
    if (ca) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        ap_log("fetch: curl error: %s", curl_easy_strerror(res));
        free(buf->data);
        buf->data = NULL;
        buf->size = 0;
        return -1;
    }

    ap_log("fetch: %s -> HTTP %ld, %zu bytes", url, http_code, buf->size);
    return 0;
}

static int fetch_worker(void *userdata) {
    fetch_task_t *task = (fetch_task_t *)userdata;
    task->result = fetch_url(task->url, &task->buf);
    return 0;
}

/* -----------------------------------------------------------------------
 * RSS/Atom XML parser
 * ----------------------------------------------------------------------- */

static const char *extract_tag(const char *xml, const char *tag,
                               char *out, size_t out_size) {
    char open_exact[64];
    char open_attr[64];
    char close[64];
    snprintf(open_exact, sizeof(open_exact), "<%s>", tag);
    snprintf(open_attr, sizeof(open_attr), "<%s ", tag);
    snprintf(close, sizeof(close), "</%s>", tag);

    const char *start = strstr(xml, open_exact);
    const char *start_attr = strstr(xml, open_attr);

    if (!start && !start_attr) return NULL;
    if (start && start_attr) {
        start = (start < start_attr) ? start : start_attr;
    } else if (!start) {
        start = start_attr;
    }

    const char *content_start = strchr(start, '>');
    if (!content_start) return NULL;
    content_start++;

    const char *end = strstr(content_start, close);
    if (!end) return NULL;

    size_t len = (size_t)(end - content_start);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, content_start, len);
    out[len] = '\0';

    return end + strlen(close);
}

static const char *extract_atom_link(const char *xml, char *out, size_t out_size) {
    out[0] = '\0';
    const char *link = xml;

    while ((link = strstr(link, "<link")) != NULL) {
        const char *tag_end = strchr(link, '>');
        if (!tag_end) return NULL;

        const char *href = strstr(link, "href=\"");
        if (href && href < tag_end) {
            href += 6;
            const char *href_end = strchr(href, '"');
            if (href_end && href_end < tag_end + 50) {
                size_t len = (size_t)(href_end - href);
                if (len >= out_size) len = out_size - 1;
                memcpy(out, href, len);
                out[len] = '\0';

                const char *rel = strstr(link, "rel=\"alternate\"");
                if (rel && rel < tag_end) {
                    return tag_end + 1;
                }

                if (out[0] != '\0') {
                    return tag_end + 1;
                }
            }
        }
        link = tag_end + 1;
    }
    return NULL;
}

static int parse_feed_xml(const char *xml) {
    g_article_count = 0;

    if (!xml || !xml[0]) return 0;

    int is_atom = (strstr(xml, "<feed") != NULL);
    int is_rss  = (strstr(xml, "<rss") != NULL || strstr(xml, "<channel") != NULL);

    if (!is_atom && !is_rss) {
        ap_log("parse: unrecognized feed format");
        return 0;
    }

    const char *item_tag = is_atom ? "entry" : "item";
    char open_exact[32], open_attr[32], close_tag[32];
    snprintf(open_exact, sizeof(open_exact), "<%s>", item_tag);
    snprintf(open_attr, sizeof(open_attr), "<%s ", item_tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", item_tag);

    const char *pos = xml;

    while (g_article_count < MAX_ARTICLES) {
        const char *item_start = strstr(pos, open_exact);
        const char *item_start_attr = strstr(pos, open_attr);

        if (!item_start && !item_start_attr) break;
        if (item_start && item_start_attr) {
            item_start = (item_start < item_start_attr) ? item_start : item_start_attr;
        } else if (!item_start) {
            item_start = item_start_attr;
        }

        const char *item_end = strstr(item_start, close_tag);
        if (!item_end) break;
        item_end += strlen(close_tag);

        size_t item_len = (size_t)(item_end - item_start);
        char *item_buf = malloc(item_len + 1);
        if (!item_buf) break;
        memcpy(item_buf, item_start, item_len);
        item_buf[item_len] = '\0';

        article_t *art = &g_articles[g_article_count];
        art->title[0] = '\0';
        art->link[0] = '\0';
        art->description[0] = '\0';
        art->pub_date[0] = '\0';
        art->domain[0] = '\0';

        extract_tag(item_buf, "title", art->title, MAX_TITLE);

        if (is_atom) {
            extract_atom_link(item_buf, art->link, MAX_URL);
        } else {
            extract_tag(item_buf, "link", art->link, MAX_URL);
        }

        if (is_atom) {
            if (!extract_tag(item_buf, "summary", art->description, MAX_DESC)) {
                extract_tag(item_buf, "content", art->description, MAX_DESC);
            }
        } else {
            extract_tag(item_buf, "description", art->description, MAX_DESC);
        }

        char raw_date[256] = {0};
        if (is_atom) {
            if (!extract_tag(item_buf, "published", raw_date, sizeof(raw_date))) {
                extract_tag(item_buf, "updated", raw_date, sizeof(raw_date));
            }
        } else {
            extract_tag(item_buf, "pubDate", raw_date, sizeof(raw_date));
        }

        free(item_buf);

        decode_xml_entities(art->title);
        strip_html_tags(art->title);
        trim_inplace(art->title);

        decode_xml_entities(art->description);
        strip_html_tags(art->description);
        trim_inplace(art->description);

        decode_xml_entities(art->link);
        trim_inplace(art->link);

        parse_date(raw_date, art->pub_date, MAX_DATE);
        extract_domain(art->link, art->domain, MAX_DOMAIN);

        if (art->title[0] != '\0') {
            ap_log("  Article %d: %s [%s] (%s)",
                   g_article_count, art->title, art->domain, art->pub_date);
            g_article_count++;
        }

        pos = item_end;
    }

    ap_log("Parsed %d article(s) (%s)", g_article_count, is_atom ? "Atom" : "RSS");
    return g_article_count;
}

/* -----------------------------------------------------------------------
 * Fetch + parse with loading indicator
 * ----------------------------------------------------------------------- */

static int fetch_and_parse(const feed_t *feed) {
    if (feed->url[0] == '\0') {
        ap_log("fetch_and_parse: no URL for feed '%s'", feed->label);
        return -1;
    }

    ap_log("Fetching feed: %s [%s]", feed->label, feed->url);

    fetch_task_t task;
    task.url = feed->url;
    task.buf.data = NULL;
    task.buf.size = 0;
    task.buf.capacity = 0;
    task.result = -1;

    char msg[256];
    snprintf(msg, sizeof(msg), "Fetching %s...", feed->label);

    ap_process_opts proc = {.message = msg,.show_progress = false,
    };

    ap_process_message(&proc, fetch_worker, &task);

    if (task.result != 0) {
        return -1;
    }

    int count = parse_feed_xml(task.buf.data);
    free(task.buf.data);
    return count;
}

/* -----------------------------------------------------------------------
 * Feed management
 * ----------------------------------------------------------------------- */

static void add_feed(void) {
    ap_keyboard_result name_result;
    int rc = ap_keyboard("", "Step 1/2: Feed name  |  Y: Cancel", AP_KB_GENERAL, &name_result);
    if (rc != AP_OK || name_result.text[0] == '\0') return;

    const char *shortcuts[] = { "https://", ".com", ".org", "/rss", "/feed", "/atom.xml" };
    ap_url_keyboard_config url_cfg = {.shortcut_keys  = shortcuts,.shortcut_count = 6,
    };
    ap_keyboard_result url_result;
    rc = ap_url_keyboard("https://", "Step 2/2: Feed URL  |  Y: Cancel", &url_cfg, &url_result);
    if (rc != AP_OK || url_result.text[0] == '\0') return;

    if (g_feed_count >= MAX_FEEDS) {
        ap_footer_item footer[] = {
            {.button = AP_BTN_A,.label = "OK",.is_confirm = true },
        };
        ap_message_opts msg = {.message      = "Maximum number of feeds reached.",.footer       = footer,.footer_count = 1,
        };
        ap_confirm_result cr;
        ap_confirmation(&msg, &cr);
        return;
    }

    strncpy(g_feeds[g_feed_count].label, name_result.text, MAX_LABEL - 1);
    strncpy(g_feeds[g_feed_count].url, url_result.text, MAX_URL - 1);
    trim_inplace(g_feeds[g_feed_count].label);
    trim_inplace(g_feeds[g_feed_count].url);
    g_feed_count++;

    ap_log("Added feed: %s [%s]", g_feeds[g_feed_count - 1].label,
           g_feeds[g_feed_count - 1].url);

    save_feeds(g_feeds_path);
}

static void edit_feed(int index) {
    if (index < 0 || index >= g_feed_count) return;

    /* Edit name — pre-fill with current name */
    ap_keyboard_result name_result;
    int rc = ap_keyboard(g_feeds[index].label,
                         "Edit feed name  |  Y: Cancel",
                         AP_KB_GENERAL, &name_result);
    if (rc != AP_OK) return;

    /* Edit URL — pre-fill with current URL */
    const char *shortcuts[] = { "https://", ".com", ".org", "/rss", "/feed", "/atom.xml" };
    ap_url_keyboard_config url_cfg = {.shortcut_keys  = shortcuts,.shortcut_count = 6,
    };
    ap_keyboard_result url_result;
    rc = ap_url_keyboard(g_feeds[index].url,
                         "Edit feed URL  |  Y: Cancel",
                         &url_cfg, &url_result);
    if (rc != AP_OK) return;

    /* Update only if user confirmed (Start) on both screens */
    if (name_result.text[0] != '\0') {
        strncpy(g_feeds[index].label, name_result.text, MAX_LABEL - 1);
        trim_inplace(g_feeds[index].label);
    }
    strncpy(g_feeds[index].url, url_result.text, MAX_URL - 1);
    trim_inplace(g_feeds[index].url);

    ap_log("Edited feed %d: %s [%s]", index, g_feeds[index].label, g_feeds[index].url);

    save_feeds(g_feeds_path);
}

static int delete_feed(int index) {
    if (index < 0 || index >= g_feed_count) return 0;

    char msg[256];
    snprintf(msg, sizeof(msg), "Delete \"%s\"?", g_feeds[index].label);

    ap_footer_item footer[] = {
        {.button = AP_BTN_B,.label = "CANCEL" },
        {.button = AP_BTN_A,.label = "DELETE",.is_confirm = true },
    };
    ap_message_opts conf = {.message      = msg,.footer       = footer,.footer_count = 2,
    };
    ap_confirm_result cr;
    ap_confirmation(&conf, &cr);

    if (cr.confirmed) {
        ap_log("Deleted feed: %s", g_feeds[index].label);

        for (int i = index; i < g_feed_count - 1; i++) {
            g_feeds[i] = g_feeds[i + 1];
        }
        g_feed_count--;

        save_feeds(g_feeds_path);
        return 1;
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * Screens
 * ----------------------------------------------------------------------- */

static int show_article_detail(int article_idx) {
    article_t *art = &g_articles[article_idx];

    ap_detail_info_pair info[2];
    int info_count = 0;

    if (art->domain[0]) {
        info[info_count].key   = "Source";
        info[info_count].value = art->domain;
        info_count++;
    }

    if (art->pub_date[0]) {
        info[info_count].key   = "Published";
        info[info_count].value = art->pub_date;
        info_count++;
    }

    ap_detail_section sections[2];
    int section_count = 0;

    if (info_count > 0) {
        memset(&sections[section_count], 0, sizeof(sections[section_count]));
        sections[section_count].type       = AP_SECTION_INFO;
        sections[section_count].title      = NULL;
        sections[section_count].info_pairs = info;
        sections[section_count].info_count = info_count;
        section_count++;
    }

    if (is_useful_description(art->description)) {
        memset(&sections[section_count], 0, sizeof(sections[section_count]));
        sections[section_count].type        = AP_SECTION_DESCRIPTION;
        sections[section_count].title       = NULL;
        sections[section_count].description = art->description;
        section_count++;
    }

    if (section_count == 0) {
        memset(&sections[0], 0, sizeof(sections[0]));
        sections[0].type        = AP_SECTION_DESCRIPTION;
        sections[0].title       = NULL;
        sections[0].description = "(No additional details available)";
        section_count = 1;
    }

    ap_footer_item footer[] = {
        {.button = AP_BTN_B,.label = "BACK" },
    };

    ap_detail_opts opts = {.title         = art->title,.sections      = sections,.section_count = section_count,.footer        = footer,.footer_count  = 1,
    };

    ap_detail_result result;
    ap_detail_screen(&opts, &result);

    return 0;
}

static int show_article_list(int feed_idx) {
    int count = fetch_and_parse(&g_feeds[feed_idx]);

    if (count <= 0) {
        char msg_text[256];
        snprintf(msg_text, sizeof(msg_text),
                 "Could not load articles from:\n%s\n\nCheck WiFi and try again.",
                 g_feeds[feed_idx].url[0] ? g_feeds[feed_idx].url : "(no URL)");

        ap_footer_item footer[] = {
            {.button = AP_BTN_A,.label = "OK",.is_confirm = true },
        };
        ap_message_opts msg = {.message      = msg_text,.footer       = footer,.footer_count = 1,
        };
        ap_confirm_result cr;
        ap_confirmation(&msg, &cr);
        return 0;
    }

    ap_list_item items[MAX_ARTICLES];
    for (int i = 0; i < g_article_count; i++) {
        memset(&items[i], 0, sizeof(items[i]));
        items[i].label = g_articles[i].title;
        items[i].metadata = NULL;
    }

    ap_footer_item footer[] = {
        {.button = AP_BTN_B,.label = "BACK" },
        {.button = AP_BTN_X,.label = "REFRESH" },
        {.button = AP_BTN_A,.label = "READ",.is_confirm = true },
    };

    int last_index = 0;
    int last_visible = 0;

    while (1) {
        ap_list_opts opts = ap_list_default_opts(
            g_feeds[feed_idx].label, items, g_article_count);
        opts.footer                  = footer;
        opts.footer_count            = 3;
        opts.initial_index           = last_index;
        opts.visible_start_index     = last_visible;
        opts.secondary_action_button = AP_BTN_X;

        ap_list_result result;
        int rc = ap_list(&opts, &result);
        last_visible = result.visible_start_index;

        if (rc != AP_OK || result.action == AP_ACTION_BACK) {
            return 0;
        }

        if (result.action == AP_ACTION_SECONDARY_TRIGGERED) {
            last_index = result.selected_index >= 0 ? result.selected_index : 0;
            count = fetch_and_parse(&g_feeds[feed_idx]);
            if (count <= 0) {
                ap_footer_item err_footer[] = {
                    {.button = AP_BTN_A,.label = "OK",.is_confirm = true },
                };
                ap_message_opts msg = {.message      = "Refresh failed.\nCheck WiFi and try again.",.footer       = err_footer,.footer_count = 1,
                };
                ap_confirm_result cr;
                ap_confirmation(&msg, &cr);
                return 0;
            }
            for (int i = 0; i < g_article_count; i++) {
                memset(&items[i], 0, sizeof(items[i]));
                items[i].label = g_articles[i].title;
                items[i].metadata = NULL;
            }
            continue;
        }

        if (result.selected_index >= 0) {
            last_index = result.selected_index;
            show_article_detail(result.selected_index);
        }
    }
}

static void manage_feed(int index) {
    if (index < 0 || index >= g_feed_count) return;

    char title[256];
    snprintf(title, sizeof(title), "Manage: %s", g_feeds[index].label);

    ap_selection_option options[] = {
        {.label = "Edit",.value = NULL },
        {.label = "Delete",.value = NULL },
    };

    ap_footer_item footer[] = {
        {.button = AP_BTN_B,.label = "CANCEL" },
        {.button = AP_BTN_A,.label = "SELECT",.is_confirm = true },
    };

    ap_selection_result result;
    int rc = ap_selection(title, options, 2, footer, 2, &result);

    if (rc != AP_OK) return;

    if (result.selected_index == 0) {
        edit_feed(index);
    } else if (result.selected_index == 1) {
        delete_feed(index);
    }
}

static int show_feed_list(void) {
    while (1) {
        ap_list_item items[MAX_FEEDS];
        for (int i = 0; i < g_feed_count; i++) {
            memset(&items[i], 0, sizeof(items[i]));
            items[i].label = g_feeds[i].label;
            items[i].metadata = NULL;
        }

        ap_footer_item footer[] = {
            {.button = AP_BTN_B,.label = "QUIT" },
            {.button = AP_BTN_X,.label = "ADD" },
            {.button = AP_BTN_SELECT,.label = "MANAGE" },
            {.button = AP_BTN_A,.label = "OPEN",.is_confirm = true },
        };

        static int last_index = 0;
        static int last_visible = 0;

        ap_list_opts opts = ap_list_default_opts("NextFeed", items, g_feed_count);
        opts.footer                  = footer;
        opts.footer_count            = 4;
        opts.initial_index           = last_index;
        opts.visible_start_index     = last_visible;
        opts.secondary_action_button = AP_BTN_X;
        opts.action_button           = AP_BTN_SELECT;

        ap_list_result result;
        int rc = ap_list(&opts, &result);
        last_visible = result.visible_start_index;

        if (rc != AP_OK || result.action == AP_ACTION_BACK) {
            return 0;
        }

        /* X — add feed */
        if (result.action == AP_ACTION_SECONDARY_TRIGGERED) {
            last_index = result.selected_index >= 0 ? result.selected_index : 0;
            add_feed();
            continue;
        }

        /* Select — manage feed (edit/delete) */
        if (result.action == AP_ACTION_TRIGGERED && result.selected_index >= 0) {
            last_index = result.selected_index;
            manage_feed(result.selected_index);
            if (last_index >= g_feed_count && g_feed_count > 0) {
                last_index = g_feed_count - 1;
            }
            continue;
        }

        /* A — open feed */
        if (result.selected_index >= 0) {
            last_index = result.selected_index;
            show_article_list(result.selected_index);
        }
    }
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    ap_config cfg = {.window_title       = "NextFeed",.log_path           = ap_resolve_log_path("nextfeed"),.is_nextui          = AP_PLATFORM_IS_DEVICE,.disable_background = true,
    };
    if (ap_init(&cfg) != AP_OK) {
        fprintf(stderr, "Failed to initialise Apostrophe\n");
        curl_global_cleanup();
        return 1;
    }
    ap_log("=== NextFeed starting ===");

    const char *feeds_path = resolve_feeds_path();
    load_feeds(feeds_path);

    if (g_feed_count == 0) {
        ap_log("WARNING: No feeds loaded.");
        ap_footer_item err_footer[] = {
            {.button = AP_BTN_B,.label = "QUIT" },
            {.button = AP_BTN_X,.label = "ADD FEED" },
        };
        ap_message_opts msg = {.message      = "No feeds configured.\nPress X to add a feed.",.image_path   = NULL,.footer       = err_footer,.footer_count = 2,
        };
        ap_confirm_result cr;
        ap_confirmation(&msg, &cr);

        if (!cr.confirmed) {
            add_feed();
        }
    }

    show_feed_list();

    ap_log("=== NextFeed shutting down ===");
    ap_quit();
    curl_global_cleanup();
    return 0;
}