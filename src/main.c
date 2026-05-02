/*
 * NextFeed — RSS/Atom reader for NextUI
 * Apostrophe (low-level) + PakKit (UI components) + libcurl + RSS/Atom parsing.
 */

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"
#define PAKKIT_UI_IMPLEMENTATION
#include "pakkit_ui.h"

#include <string.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <time.h>

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
#define MAX_PATH_LEN 1024
#define MIN_USEFUL_DESC 50
#define CACHE_MAX_AGE_SECS (60 * 60)
#define NEXTFEED_VERSION "0.2.0"

/* -----------------------------------------------------------------------
 * Data structures
 * ----------------------------------------------------------------------- */

typedef struct {
    char label[MAX_LABEL];
    char url[MAX_URL];
    int  cached_article_count;
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

typedef struct {
    feed_t *feeds;
    int     feed_count;
    int     refreshed;
    int     failed;
    char   *status_msg;
} refresh_all_task_t;

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */

static feed_t    g_feeds[MAX_FEEDS];
static int       g_feed_count = 0;

static article_t g_articles[MAX_ARTICLES];
static int       g_article_count = 0;

static char      g_feeds_path[MAX_PATH_LEN] = {0};
static char      g_cache_dir[MAX_PATH_LEN] = {0};
static char      g_config_dir[MAX_PATH_LEN] = {0};

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
            else if (strncmp(r, "&ldquo;", 7) == 0)   { *w++ = '"'; r += 7; }
            else if (strncmp(r, "&rdquo;", 7) == 0)   { *w++ = '"'; r += 7; }
            else if (strncmp(r, "&lsquo;", 7) == 0)   { *w++ = '\''; r += 7; }
            else if (strncmp(r, "&rsquo;", 7) == 0)   { *w++ = '\''; r += 7; }
            else if (strncmp(r, "&mdash;", 7) == 0)   { *w++ = '-'; *w++ = '-'; r += 7; }
            else if (strncmp(r, "&ndash;", 7) == 0)   { *w++ = '-'; r += 7; }
            else if (strncmp(r, "&hellip;", 8) == 0)  { *w++ = '.'; *w++ = '.'; *w++ = '.'; r += 8; }
            else if (strncmp(r, " ", 6) == 0)    { *w++ = ' '; r += 6; }
            else if (strncmp(r, "&copy;", 6) == 0)    { *w++ = '('; *w++ = 'c'; *w++ = ')'; r += 6; }
            else if (strncmp(r, "&reg;", 5) == 0)     { *w++ = '('; *w++ = 'R'; *w++ = ')'; r += 5; }
            else if (strncmp(r, "&trade;", 7) == 0)   { *w++ = 'T'; *w++ = 'M'; r += 7; }
            else if (r[1] == '#') {
                unsigned int codepoint = 0;
                char *end = NULL;
                if (r[2] == 'x' || r[2] == 'X') {
                    const char *hex_start = r + 3;
                    char *hex_end = NULL;
                    codepoint = (unsigned int)strtoul(hex_start, &hex_end, 16);
                    if (hex_end && *hex_end == ';') end = hex_end + 1;
                } else {
                    const char *dec_start = r + 2;
                    char *dec_end = NULL;
                    codepoint = (unsigned int)strtoul(dec_start, &dec_end, 10);
                    if (dec_end && *dec_end == ';') end = dec_end + 1;
                }
                if (end && codepoint > 0) {
                    if (codepoint < 128)                             { *w++ = (char)codepoint; }
                    else if (codepoint == 8216 || codepoint == 8217) { *w++ = '\''; }
                    else if (codepoint == 8220 || codepoint == 8221) { *w++ = '"'; }
                    else if (codepoint == 8211)                      { *w++ = '-'; }
                    else if (codepoint == 8212)                      { *w++ = '-'; *w++ = '-'; }
                    else if (codepoint == 8230)                      { *w++ = '.'; *w++ = '.'; *w++ = '.'; }
                    else if (codepoint == 160)                       { *w++ = ' '; }
                    else if (codepoint == 169)                       { *w++ = '('; *w++ = 'c'; *w++ = ')'; }
                    else if (codepoint == 174)                       { *w++ = '('; *w++ = 'R'; *w++ = ')'; }
                    else if (codepoint == 8226)                      { *w++ = '*'; }
                    else if (codepoint == 8364)                      { *w++ = 'E'; }
                    else if (codepoint == 163)                       { *w++ = 'L'; }
                    else                                             { *w++ = '?'; }
                    r = end;
                } else { *w++ = *r++; }
            }
            else { *w++ = *r++; }
        } else { *w++ = *r++; }
    }
    *w = '\0';
}

static void strip_html_tags(char *s) {
    char *r = s, *w = s;
    int in_tag = 0, in_cdata = 0;
    while (*r) {
        if (strncmp(r, "<![CDATA[", 9) == 0) { r += 9; in_cdata = 1; continue; }
        if (in_cdata && strncmp(r, "]]>", 3) == 0) { r += 3; in_cdata = 0; continue; }
        if (in_cdata) {
            if (*r == '<') { in_tag = 1; r++; continue; }
            if (*r == '>' && in_tag) { in_tag = 0; r++; continue; }
            if (!in_tag) *w++ = *r;
            r++; continue;
        }
        if (*r == '<') { in_tag = 1; r++; continue; }
        if (*r == '>' && in_tag) { in_tag = 0; r++; continue; }
        if (!in_tag) *w++ = *r;
        r++;
    }
    *w = '\0';
}

/* -----------------------------------------------------------------------
 * Domain / Date / Description helpers
 * ----------------------------------------------------------------------- */

static void extract_domain(const char *url, char *domain, size_t domain_size) {
    domain[0] = '\0';
    if (!url || !url[0]) return;
    const char *start = strstr(url, "://");
    if (start) { start += 3; } else { start = url; }
    if (strncmp(start, "www.", 4) == 0) start += 4;
    const char *end = strchr(start, '/');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= domain_size) len = domain_size - 1;
    memcpy(domain, start, len);
    domain[len] = '\0';
}

static const char *month_names[] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

static int month_from_name(const char *name) {
    for (int i = 0; i < 12; i++)
        if (strncasecmp(name, month_names[i], 3) == 0) return i + 1;
    return 0;
}

static void parse_rfc2822_date(const char *raw, char *out, size_t out_size) {
    out[0] = '\0';
    const char *p = strchr(raw, ',');
    if (p) { p++; while (*p == ' ') p++; } else { p = raw; }
    int day = 0; char mon_str[16] = {0}; int year = 0, hour = 0, min = 0;
    if (sscanf(p, "%d %15s %d %d:%d", &day, mon_str, &year, &hour, &min) >= 3) {
        int month = month_from_name(mon_str);
        if (month > 0 && day > 0 && year > 0) {
            if (hour >= 0 && min >= 0)
                snprintf(out, out_size, "%s %d, %d %02d:%02d", month_names[month-1], day, year, hour, min);
            else
                snprintf(out, out_size, "%s %d, %d", month_names[month-1], day, year);
        }
    }
}

static void parse_iso8601_date(const char *raw, char *out, size_t out_size) {
    out[0] = '\0';
    int year = 0, month = 0, day = 0, hour = 0, min = 0;
    if (sscanf(raw, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &min) >= 3) {
        if (month >= 1 && month <= 12 && day > 0 && year > 0) {
            if (hour >= 0 && min >= 0)
                snprintf(out, out_size, "%s %d, %d %02d:%02d", month_names[month-1], day, year, hour, min);
            else
                snprintf(out, out_size, "%s %d, %d", month_names[month-1], day, year);
        }
    }
}

static void parse_date(const char *raw, char *out, size_t out_size) {
    out[0] = '\0';
    if (!raw || !raw[0]) return;
    if (raw[0] >= '0' && raw[0] <= '9') parse_iso8601_date(raw, out, out_size);
    else parse_rfc2822_date(raw, out, out_size);
}

static int is_useful_description(const char *desc) {
    if (!desc || !desc[0]) return 0;
    if (strlen(desc) < MIN_USEFUL_DESC) return 0;
    if (strcasecmp(desc, "Comments") == 0) return 0;
    return 1;
}

/* -----------------------------------------------------------------------
 * Cache helpers
 * ----------------------------------------------------------------------- */

static void url_to_cache_filename(const char *url, char *out, size_t out_size) {
    unsigned long hash = 5381;
    const char *p = url;
    while (*p) { hash = ((hash << 5) + hash) + (unsigned char)*p; p++; }
    snprintf(out, out_size, "%08lx.xml", hash);
}

static void get_cache_path(const char *url, char *out, size_t out_size) {
    char filename[64];
    url_to_cache_filename(url, filename, sizeof(filename));
    snprintf(out, out_size, "%s/%s", g_cache_dir, filename);
}

static int cache_exists(const char *url) {
    if (g_cache_dir[0] == '\0' || !url || !url[0]) return 0;
    char path[MAX_PATH_LEN];
    get_cache_path(url, path, sizeof(path));
    return access(path, R_OK) == 0;
}

static long cache_age_secs(const char *url) {
    if (g_cache_dir[0] == '\0' || !url || !url[0]) return -1;
    char path[MAX_PATH_LEN];
    get_cache_path(url, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    time_t now = time(NULL);
    return (long)(now - st.st_mtime);
}

static int save_to_cache(const char *url, const char *data, size_t size) {
    if (g_cache_dir[0] == '\0') return -1;
    char path[MAX_PATH_LEN];
    get_cache_path(url, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) { ap_log("cache: could not write %s", path); return -1; }
    fwrite(data, 1, size, f);
    fclose(f);
    ap_log("cache: saved %zu bytes to %s", size, path);
    return 0;
}

static char *load_from_cache(const char *url) {
    char path[MAX_PATH_LEN];
    get_cache_path(url, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) { ap_log("cache: no cache for %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return NULL; }
    char *data = malloc((size_t)fsize + 1);
    if (!data) { fclose(f); return NULL; }
    size_t read_bytes = fread(data, 1, (size_t)fsize, f);
    fclose(f);
    data[read_bytes] = '\0';
    ap_log("cache: loaded %zu bytes from %s", read_bytes, path);
    return data;
}

static void clear_feed_cache(const char *url) {
    if (g_cache_dir[0] == '\0' || !url || !url[0]) return;
    char path[MAX_PATH_LEN];
    get_cache_path(url, path, sizeof(path));
    if (remove(path) == 0) ap_log("cache: cleared %s", path);
}

static int count_cached_articles(const char *url) {
    if (!cache_exists(url)) return -1;
    char *xml = load_from_cache(url);
    if (!xml) return -1;
    int is_atom = (strstr(xml, "<feed") != NULL);
    const char *tag = is_atom ? "<entry" : "<item";
    int count = 0;
    const char *p = xml;
    while ((p = strstr(p, tag)) != NULL) { count++; p++; }
    free(xml);
    return count;
}

/* -----------------------------------------------------------------------
 * Feed config loading and saving
 * ----------------------------------------------------------------------- */

static int load_feeds(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { ap_log("Could not open feeds file: %s", path); return 0; }
    char line[MAX_LINE];
    g_feed_count = 0;
    while (fgets(line, sizeof(line), f) && g_feed_count < MAX_FEEDS) {
        trim_inplace(line);
        if (line[0] == '\0' || line[0] == '#') continue;
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
        g_feeds[g_feed_count].cached_article_count = -1;
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
    if (!f) { ap_log("Could not write feeds file: %s", path); return -1; }
    fprintf(f, "# NextFeed — Feed Configuration\n");
    fprintf(f, "# Format: Label|URL (one per line)\n\n");
    for (int i = 0; i < g_feed_count; i++) {
        if (g_feeds[i].url[0])
            fprintf(f, "%s|%s\n", g_feeds[i].label, g_feeds[i].url);
        else
            fprintf(f, "%s\n", g_feeds[i].label);
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

static void update_article_counts(void) {
    for (int i = 0; i < g_feed_count; i++) {
        if (g_feeds[i].url[0])
            g_feeds[i].cached_article_count = count_cached_articles(g_feeds[i].url);
        else
            g_feeds[i].cached_article_count = -1;
    }
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
    buf->data = NULL; buf->size = 0; buf->capacity = 0;
    CURL *curl = curl_easy_init();
    if (!curl) { ap_log("fetch: curl_easy_init failed"); return -1; }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NextFeed/0.2");
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
        free(buf->data); buf->data = NULL; buf->size = 0;
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
 * Auto-refresh
 * ----------------------------------------------------------------------- */

static int refresh_all_worker(void *userdata) {
    refresh_all_task_t *task = (refresh_all_task_t *)userdata;
    task->refreshed = 0; task->failed = 0;
    for (int i = 0; i < task->feed_count; i++) {
        if (task->feeds[i].url[0] == '\0') continue;
        long age = cache_age_secs(task->feeds[i].url);
        if (age >= 0 && age < CACHE_MAX_AGE_SECS) {
            ap_log("refresh_all: %s cache is %ld sec old, skipping", task->feeds[i].label, age);
            continue;
        }
        ap_log("refresh_all: fetching %s", task->feeds[i].label);
        if (task->status_msg)
            snprintf(task->status_msg, 256, "Refreshing %s... (%d/%d)",
                     task->feeds[i].label, i + 1, task->feed_count);
        fetch_buf_t buf;
        int rc = fetch_url(task->feeds[i].url, &buf);
        if (rc == 0 && buf.data) {
            save_to_cache(task->feeds[i].url, buf.data, buf.size);
            task->refreshed++;
            free(buf.data);
        } else { task->failed++; }
    }
    return 0;
}

static void auto_refresh_feeds(void) {
    int stale_count = 0;
    for (int i = 0; i < g_feed_count; i++) {
        if (g_feeds[i].url[0] == '\0') continue;
        long age = cache_age_secs(g_feeds[i].url);
        if (age < 0 || age >= CACHE_MAX_AGE_SECS) stale_count++;
    }
    if (stale_count == 0) { ap_log("auto_refresh: all feeds are fresh"); return; }
    ap_log("auto_refresh: %d feed(s) need refreshing", stale_count);
    char status_buf[256] = "Refreshing feeds...";
    char *status_ptr = status_buf;
    refresh_all_task_t task = {.feeds = g_feeds,.feed_count = g_feed_count,.refreshed = 0,.failed = 0,.status_msg = status_buf,
    };
    ap_process_opts proc = {.message = "Refreshing feeds...",.show_progress = false,.dynamic_message = &status_ptr,.message_lines = 2,
    };
    ap_process_message(&proc, refresh_all_worker, &task);
    ap_log("auto_refresh: done. %d refreshed, %d failed", task.refreshed, task.failed);
    update_article_counts();
}

/* -----------------------------------------------------------------------
 * RSS/Atom XML parser
 * ----------------------------------------------------------------------- */

static const char *extract_tag(const char *xml, const char *tag,
                               char *out, size_t out_size) {
    char open_exact[64], open_attr[64], close[64];
    snprintf(open_exact, sizeof(open_exact), "<%s>", tag);
    snprintf(open_attr, sizeof(open_attr), "<%s ", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *start = strstr(xml, open_exact);
    const char *start_attr = strstr(xml, open_attr);
    if (!start && !start_attr) return NULL;
    if (start && start_attr)
        start = (start < start_attr) ? start : start_attr;
    else if (!start) start = start_attr;
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
                if (rel && rel < tag_end) return tag_end + 1;
                if (out[0] != '\0') return tag_end + 1;
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
    if (!is_atom && !is_rss) { ap_log("parse: unrecognized feed format"); return 0; }
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
        if (item_start && item_start_attr)
            item_start = (item_start < item_start_attr) ? item_start : item_start_attr;
        else if (!item_start) item_start = item_start_attr;
        const char *item_end = strstr(item_start, close_tag);
        if (!item_end) break;
        item_end += strlen(close_tag);
        size_t item_len = (size_t)(item_end - item_start);
        char *item_buf = malloc(item_len + 1);
        if (!item_buf) break;
        memcpy(item_buf, item_start, item_len);
        item_buf[item_len] = '\0';
        article_t *art = &g_articles[g_article_count];
        memset(art, 0, sizeof(*art));
        extract_tag(item_buf, "title", art->title, MAX_TITLE);
        if (is_atom) extract_atom_link(item_buf, art->link, MAX_URL);
        else extract_tag(item_buf, "link", art->link, MAX_URL);
        if (is_atom) {
            if (!extract_tag(item_buf, "summary", art->description, MAX_DESC))
                extract_tag(item_buf, "content", art->description, MAX_DESC);
        } else {
            extract_tag(item_buf, "description", art->description, MAX_DESC);
        }
        char raw_date[256] = {0};
        if (is_atom) {
            if (!extract_tag(item_buf, "published", raw_date, sizeof(raw_date)))
                extract_tag(item_buf, "updated", raw_date, sizeof(raw_date));
        } else {
            extract_tag(item_buf, "pubDate", raw_date, sizeof(raw_date));
        }
        free(item_buf);
        decode_xml_entities(art->title); strip_html_tags(art->title);
        decode_xml_entities(art->title); trim_inplace(art->title);
        decode_xml_entities(art->description); strip_html_tags(art->description);
        decode_xml_entities(art->description); trim_inplace(art->description);
        decode_xml_entities(art->link); decode_xml_entities(art->link);
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
 * Fetch + parse with caching
 * ----------------------------------------------------------------------- */

static int load_from_cache_and_parse(const feed_t *feed) {
    if (feed->url[0] == '\0') return -1;
    char *xml = load_from_cache(feed->url);
    if (!xml) return -1;
    int count = parse_feed_xml(xml);
    free(xml);
    return count;
}

static int fetch_cache_and_parse(const feed_t *feed) {
    if (feed->url[0] == '\0') {
        ap_log("fetch_cache_and_parse: no URL for feed '%s'", feed->label);
        return -1;
    }
    ap_log("Fetching feed: %s [%s]", feed->label, feed->url);
    fetch_task_t task;
    task.url = feed->url;
    task.buf.data = NULL; task.buf.size = 0; task.buf.capacity = 0;
    task.result = -1;
    char msg[256];
    snprintf(msg, sizeof(msg), "Fetching %s...", feed->label);
    ap_process_opts proc = {.message = msg,.show_progress = false };
    ap_process_message(&proc, fetch_worker, &task);
    if (task.result != 0) return -1;
    save_to_cache(feed->url, task.buf.data, task.buf.size);
    int count = parse_feed_xml(task.buf.data);
    free(task.buf.data);
    return count;
}

static int open_feed(const feed_t *feed) {
    int count = load_from_cache_and_parse(feed);
    if (count > 0) {
        ap_log("Loaded %d articles from cache for '%s'", count, feed->label);
        return count;
    }
    return fetch_cache_and_parse(feed);
}

/* -----------------------------------------------------------------------
 * Feed management
 * ----------------------------------------------------------------------- */

static void add_feed(void) {
    pakkit_keyboard_result name_result;
    pakkit_keyboard_opts name_opts = {.prompt = "Step 1/2: Feed name" };
    int rc = pakkit_keyboard("", &name_opts, &name_result);
    if (rc != AP_OK || name_result.text[0] == '\0') return;

    const char *shortcuts[] = { ".com", ".org", ".io", "/rss", "/feed", "/atom", ".xml", ".rss" };
    pakkit_keyboard_opts url_opts = {.prompt = "Step 2/2: Feed URL",.shortcuts = shortcuts,.shortcut_count = 8,
    };
    pakkit_keyboard_result url_result;
    rc = pakkit_keyboard("https://", &url_opts, &url_result);
    if (rc != AP_OK || url_result.text[0] == '\0') return;

    if (g_feed_count >= MAX_FEEDS) {
        pakkit_message("Maximum number of feeds reached.", "OK");
        return;
    }
    strncpy(g_feeds[g_feed_count].label, name_result.text, MAX_LABEL - 1);
    strncpy(g_feeds[g_feed_count].url, url_result.text, MAX_URL - 1);
    trim_inplace(g_feeds[g_feed_count].label);
    trim_inplace(g_feeds[g_feed_count].url);
    g_feeds[g_feed_count].cached_article_count = -1;
    g_feed_count++;
    ap_log("Added feed: %s [%s]", g_feeds[g_feed_count - 1].label, g_feeds[g_feed_count - 1].url);
    save_feeds(g_feeds_path);
}

static void edit_feed(int index) {
    if (index < 0 || index >= g_feed_count) return;

    pakkit_keyboard_opts name_opts = {.prompt = "Edit feed name" };
    pakkit_keyboard_result name_result;
    int rc = pakkit_keyboard(g_feeds[index].label, &name_opts, &name_result);
    if (rc != AP_OK) return;

    const char *shortcuts[] = { ".com", ".org", ".io", "/rss", "/feed", "/atom", ".xml", ".rss" };
    pakkit_keyboard_opts url_opts = {.prompt = "Step 2/2: Feed URL",.shortcuts = shortcuts,.shortcut_count = 8,
    };
    pakkit_keyboard_result url_result;
    rc = pakkit_keyboard(g_feeds[index].url, &url_opts, &url_result);
    if (rc != AP_OK) return;

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
    if (pakkit_confirm(msg, "Delete", "Cancel")) {
        ap_log("Deleted feed: %s", g_feeds[index].label);
        clear_feed_cache(g_feeds[index].url);
        for (int i = index; i < g_feed_count - 1; i++)
            g_feeds[i] = g_feeds[i + 1];
        g_feed_count--;
        save_feeds(g_feeds_path);
        return 1;
    }
    return 0;
}

static void move_feed(int index, int direction) {
    int new_index = index + direction;
    if (new_index < 0 || new_index >= g_feed_count) return;
    feed_t tmp = g_feeds[index];
    g_feeds[index] = g_feeds[new_index];
    g_feeds[new_index] = tmp;
    save_feeds(g_feeds_path);
    ap_log("Moved feed '%s' from %d to %d", g_feeds[new_index].label, index, new_index);
}

/* -----------------------------------------------------------------------
 * About screen
 * ----------------------------------------------------------------------- */

static void show_about(void) {
    pakkit_info_pair info[] = {
        {.key = "Version",.value = NEXTFEED_VERSION },
        {.key = "Platform",.value = AP_PLATFORM_NAME },
        {.key = "UI",.value = "PakKit + Apostrophe" },
        {.key = "License",.value = "MIT" },
    };
    const char *credits[] = {
        "NextFeed by Eric Reinsmidt",
        "Built with PakKit + Apostrophe by Helaas",
        "For NextUI by LoveRetro",
    };
    pakkit_detail_opts opts = {.title        = "NextFeed",.subtitle     = "RSS/Atom reader for NextUI",.info         = info,.info_count   = 4,.credits      = credits,.credit_count = 3,
    };
    pakkit_detail_screen(&opts);
}

/* -----------------------------------------------------------------------
 * Manage feed menu
 * ----------------------------------------------------------------------- */

static void manage_feed(int index, int *new_index) {
    if (index < 0 || index >= g_feed_count) return;
    *new_index = index;

    char title[256];
    snprintf(title, sizeof(title), "Manage: %s", g_feeds[index].label);

    pakkit_menu_item items[5];
    int item_count = 4;
    items[0] = (pakkit_menu_item){.label = "Edit" };
    items[1] = (pakkit_menu_item){.label = "Delete" };
    items[2] = (pakkit_menu_item){.label = "Move Up" };
    items[3] = (pakkit_menu_item){.label = "Move Down" };

    if (cache_exists(g_feeds[index].url)) {
        items[4] = (pakkit_menu_item){.label = "Clear Cache" };
        item_count = 5;
    }

    pakkit_menu_result result;
    int rc = pakkit_menu(title, items, item_count, &result);
    if (rc != AP_OK) return;

    switch (result.selected_index) {
        case 0: edit_feed(index); break;
        case 1:
            if (delete_feed(index)) {
                if (*new_index >= g_feed_count && g_feed_count > 0)
                    *new_index = g_feed_count - 1;
            }
            break;
        case 2:
            if (index > 0) { move_feed(index, -1); *new_index = index - 1; }
            break;
        case 3:
            if (index < g_feed_count - 1) { move_feed(index, 1); *new_index = index + 1; }
            break;
        case 4:
            clear_feed_cache(g_feeds[index].url);
            g_feeds[index].cached_article_count = -1;
            pakkit_message("Cache cleared.", "OK");
            break;
    }
}

/* -----------------------------------------------------------------------
 * Main menu (Y button)
 * ----------------------------------------------------------------------- */

static void show_menu(int feed_index, int *new_index) {
    *new_index = feed_index;

    pakkit_menu_item items[] = {
        {.label = "Manage Feed" },
        {.label = "About" },
    };

    pakkit_menu_result result;
    int rc = pakkit_menu("Menu", items, 2, &result);
    if (rc != AP_OK) return;

    switch (result.selected_index) {
        case 0:
            if (feed_index >= 0 && feed_index < g_feed_count)
                manage_feed(feed_index, new_index);
            break;
        case 1:
            show_about();
            break;
    }
}

/* -----------------------------------------------------------------------
 * Screens
 * ----------------------------------------------------------------------- */

static int show_article_detail(int article_idx) {
    article_t *art = &g_articles[article_idx];

    char subtitle[256] = {0};
    if (art->domain[0] && art->pub_date[0])
        snprintf(subtitle, sizeof(subtitle), "%s  |  %s", art->domain, art->pub_date);
    else if (art->domain[0])
        snprintf(subtitle, sizeof(subtitle), "%s", art->domain);
    else if (art->pub_date[0])
        snprintf(subtitle, sizeof(subtitle), "%s", art->pub_date);

    int running = 1;
    int scroll_y = 0;

    while (running) {
        ap_input_event ev;
        while (ap_poll_input(&ev)) {
            if (ev.pressed) {
                switch (ev.button) {
                    case AP_BTN_B:
                        if (!ev.repeated) running = 0;
                        break;
                    case AP_BTN_UP:
                        if (scroll_y > 0) scroll_y -= PAKKIT_SCROLL_STEP;
                        if (scroll_y < 0) scroll_y = 0;
                        break;
                    case AP_BTN_DOWN:
                        scroll_y += PAKKIT_SCROLL_STEP;
                        break;
                    default:
                        break;
                }
            }
        }

        ap_clear_screen();
        ap_draw_background();

        int sw = ap_get_screen_width();
        int sh = ap_get_screen_height();
        int pad = AP_DS(5);

        TTF_Font *font_large = ap_get_font(AP_FONT_LARGE);
        TTF_Font *font_small = ap_get_font(AP_FONT_SMALL);
        TTF_Font *font_tiny  = ap_get_font(AP_FONT_TINY);

        ap_theme *theme = ap_get_theme();
        ap_color text_color = theme->text;
        ap_color hint_color = theme->hint;

        int hint_font_h = TTF_FontHeight(font_tiny);
        int footer_h = hint_font_h + pad * 2;
        int content_top = pad;
        int content_bottom = sh - footer_h;
        int content_h = content_bottom - content_top;

        SDL_Rect clip = { 0, content_top, sw, content_h };
        SDL_RenderSetClipRect(ap__g.renderer, &clip);

        int y = content_top - scroll_y;
        int text_w = sw - pad * 6;

        /* Title (wrapped) */
        int title_h = ap_measure_wrapped_text_height(font_large, art->title, text_w);
        ap_draw_text_wrapped(font_large, art->title, pad * 3, y, text_w,
                             text_color, AP_ALIGN_LEFT);
        y += title_h + pad;

        /* Subtitle (source | date) */
        if (subtitle[0]) {
            ap_draw_text(font_small, subtitle, pad * 3, y, hint_color);
            y += TTF_FontHeight(font_small) + pad * 3;
        } else {
            y += pad * 2;
        }

        /* Divider */
        ap_draw_rect(pad * 3, y, sw - pad * 6, 1, hint_color);
        y += pad * 3;

        /* Description body */
        if (is_useful_description(art->description)) {
            int desc_h = ap_measure_wrapped_text_height(font_small, art->description, text_w);
            ap_draw_text_wrapped(font_small, art->description, pad * 3, y, text_w,
                                 text_color, AP_ALIGN_LEFT);
            y += desc_h + pad * 2;
        } else {
            ap_draw_text(font_small, "(No additional details available)", pad * 3, y, hint_color);
            y += TTF_FontHeight(font_small) + pad * 2;
        }

        /* Clamp scroll */
        int total_content = y + scroll_y - content_top;
        int max_scroll = total_content - content_h;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_y > max_scroll) scroll_y = max_scroll;

        SDL_RenderSetClipRect(ap__g.renderer, NULL);

        /* Hints */
        pakkit_hint hints[] = {
            {.button = "B",.label = "Back" },
        };
        pakkit_draw_hints(hints, 1);

        ap_present();
    }
    return 0;
}

static int show_article_list(int feed_idx) {
    int count = open_feed(&g_feeds[feed_idx]);
    if (count <= 0) {
        char msg_text[256];
        snprintf(msg_text, sizeof(msg_text),
                 "Could not load articles from:\n%s\n\nCheck WiFi and try again.",
                 g_feeds[feed_idx].url[0] ? g_feeds[feed_idx].url : "(no URL)");
        pakkit_message(msg_text, "OK");
        return 0;
    }
    g_feeds[feed_idx].cached_article_count = g_article_count;

    pakkit_list_item items[MAX_ARTICLES];
    for (int i = 0; i < g_article_count; i++)
        items[i] = (pakkit_list_item){.label = g_articles[i].title };

    pakkit_hint hints[] = {
        {.button = "B",.label = "Back" },
        {.button = "X",.label = "Refresh" },
        {.button = "A",.label = "Read" },
    };

    int last_index = 0;
    while (1) {
        pakkit_list_opts opts = {.title            = g_feeds[feed_idx].label,.hints            = hints,.hint_count       = 3,.secondary_button = AP_BTN_X,.tertiary_button  = AP_BTN_NONE,.initial_index    = last_index,
        };
        pakkit_list_result result;
        pakkit_list(&opts, items, g_article_count, &result);

        if (result.action == PAKKIT_ACTION_BACK) return 0;

        if (result.action == PAKKIT_ACTION_SECONDARY) {
            last_index = result.selected_index >= 0 ? result.selected_index : 0;
            count = fetch_cache_and_parse(&g_feeds[feed_idx]);
            if (count <= 0) {
                pakkit_message("Refresh failed.\nCheck WiFi and try again.", "OK");
                return 0;
            }
            g_feeds[feed_idx].cached_article_count = g_article_count;
            for (int i = 0; i < g_article_count; i++)
                items[i] = (pakkit_list_item){.label = g_articles[i].title };
            continue;
        }

        if (result.selected_index >= 0) {
            last_index = result.selected_index;
            show_article_detail(result.selected_index);
        }
    }
}

static int show_feed_list(void) {
    int last_index = 0;

    while (1) {
        pakkit_list_item items[MAX_FEEDS];
        static char feed_labels[MAX_FEEDS][MAX_LABEL + 16];
        for (int i = 0; i < g_feed_count; i++) {
            int cnt = g_feeds[i].cached_article_count;
            if (cnt > 0)
                snprintf(feed_labels[i], sizeof(feed_labels[i]), "%s (%d)", g_feeds[i].label, cnt);
            else
                snprintf(feed_labels[i], sizeof(feed_labels[i]), "%s", g_feeds[i].label);
            items[i] = (pakkit_list_item){.label = feed_labels[i] };
        }

        pakkit_hint hints[] = {
            {.button = "B",.label = "Quit" },
            {.button = "X",.label = "Add" },
            {.button = "Y",.label = "Menu" },
            {.button = "A",.label = "Open" },
        };

        pakkit_list_opts opts = {.title            = "NextFeed",.hints            = hints,.hint_count       = 4,.secondary_button = AP_BTN_X,.tertiary_button  = AP_BTN_Y,.initial_index    = last_index,
        };

        pakkit_list_result result;
        pakkit_list(&opts, items, g_feed_count, &result);

        if (result.action == PAKKIT_ACTION_BACK) return 0;

        /* X — add feed */
        if (result.action == PAKKIT_ACTION_SECONDARY) {
            last_index = result.selected_index >= 0 ? result.selected_index : 0;
            add_feed();
            update_article_counts();
            continue;
        }
        /* Y — menu */
        if (result.action == PAKKIT_ACTION_TERTIARY) {
            last_index = result.selected_index >= 0 ? result.selected_index : 0;
            show_menu(last_index, &last_index);
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

    const char *cache_env = getenv("NEXTFEED_CACHE_DIR");
    if (cache_env) strncpy(g_cache_dir, cache_env, sizeof(g_cache_dir) - 1);

    const char *config_env = getenv("NEXTFEED_CONFIG_DIR");
    if (config_env) strncpy(g_config_dir, config_env, sizeof(g_config_dir) - 1);

    /* Truncate log before ap_init so it only contains the current session */
    {
        const char *lp = ap_resolve_log_path("nextfeed");
        if (lp) {
            FILE *lf = fopen(lp, "w");
            if (lf) fclose(lf);
        }
    }

    ap_config cfg = {.window_title       = "NextFeed",.log_path           = ap_resolve_log_path("nextfeed"),.is_nextui          = AP_PLATFORM_IS_DEVICE,.disable_background = true,
    };
    if (ap_init(&cfg) != AP_OK) {
        fprintf(stderr, "Failed to initialise Apostrophe\n");
        curl_global_cleanup();
        return 1;
    }

    /* Apply default theme colors */
    ap_theme *theme = ap_get_theme();
    theme->background = (ap_color){30, 30, 35, 255};
    theme->text       = (ap_color){220, 220, 220, 255};
    theme->hint       = (ap_color){140, 140, 150, 255};

    ap_log("=== NextFeed v%s starting ===", NEXTFEED_VERSION);
    ap_log("Cache dir: %s", g_cache_dir[0] ? g_cache_dir : "(none)");
    ap_log("Config dir: %s", g_config_dir[0] ? g_config_dir : "(none)");

    const char *feeds_path = resolve_feeds_path();
    load_feeds(feeds_path);

    if (g_feed_count == 0) {
        ap_log("WARNING: No feeds loaded.");
        pakkit_message("No feeds configured.\nPress A to add a feed.", "Add Feed");
        add_feed();
    }

    if (g_feed_count > 0) auto_refresh_feeds();
    update_article_counts();

    show_feed_list();

    ap_log("=== NextFeed shutting down ===");
    ap_quit();
    curl_global_cleanup();
    return 0;
}