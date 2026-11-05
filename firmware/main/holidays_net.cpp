#include "holidays.h"
#include "holidays_parse.h"
#include "store.h"
#include "clock_time.h"
#include "pricing.h"
#include "wifi_prov.h"
#include "app_config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>

static const char *TAG = "holidays";

// ---------------------------------------------------------------------------
// Three independent sources, tried in order.
//
// No single endpoint was reliable enough alone: timor.tech carries the most detail
// but refused the connection from this board, and a holiday table with one upstream
// is one outage away from being stale. Each entry below is a different operator, and
// every response shape is normalised to the same four day kinds.
//
//   daily  - asked once a day for the day being displayed. This is the
//            authoritative answer, and checking daily is what keeps up with an
//            arrangement that is amended after publication.
//   yearly - asked monthly. Cannot be avoided: a single-day query says nothing
//            about a future day, and the panel shows the next switch time, which can
//            fall on one.
//
// Everything is persisted, and everything falls back to the compiled table, so no
// outage can change what the panel shows - it only stops the board from learning.
// ---------------------------------------------------------------------------


// Which yearly-payload shape a source answers with. It used to be inferred by matching
// substrings of the source name, so adding a host whose name happened to contain
// "chinese-days" silently selected that parser - and a rename would have broken a working
// source with no compile error. The shape belongs in the table.
typedef enum {
    HOL_PARSER_DAY_ONLY = 0,     // no yearly endpoint
    HOL_PARSER_TIMOR,
    HOL_PARSER_XIAOAI,
    HOL_PARSER_CHINESE_DAYS,
} hol_parser_t;

typedef struct {
    const char *name;
    const char *daily_fmt;   // %04d-%02d-%02d
    const char *year_fmt;    // %d; null for a day-only service
    hol_parser_t parser;
} hol_source_t;

// Ordered so the reachable sources are tried first.
//
// Measured from this board: timor.tech and publicapi.xiaoai.me both resolve but
// then fail to connect. They sit behind Cloudflare (104.21.x.x and 172.67.x.x) and
// that path is not reachable here, while holiday.dreace.top is a directly hosted Go
// service and answers fine. There is no point paying two timeouts before the one
// that works, so dreace leads; the others stay because the reachability of any
// single host is not something this firmware can assume.
//
// chinese-days is a static JSON release rather than a query API, which suits the
// yearly slot: it publishes whole years and is updated by an automated pull request
// when the State Council publishes. Both of its CDN hosts are listed so one being
// unreachable does not lose the source.
static const hol_source_t kSources[] = {
    {
        "dreace",
        "https://holiday.dreace.top/%04d-%02d-%02d",
        nullptr,        // day-only service
        HOL_PARSER_DAY_ONLY,
    },
    {
        "timor.tech",
        "https://timor.tech/api/holiday/info/%04d-%02d-%02d",
        "https://timor.tech/api/holiday/year/%d",
        HOL_PARSER_TIMOR,
    },
    {
        "xiaoai.me",
        "https://publicapi.xiaoai.me/holiday/day?date=%04d-%02d-%02d",
        "https://publicapi.xiaoai.me/holiday/year?date=%d",
        HOL_PARSER_XIAOAI,
    },
    {
        "chinese-days/jsdelivr",
        nullptr,
        "https://cdn.jsdelivr.net/npm/chinese-days/dist/years/%d.json",
        HOL_PARSER_CHINESE_DAYS,
    },
    {
        "chinese-days/fastly",
        nullptr,
        "https://fastly.jsdelivr.net/npm/chinese-days/dist/years/%d.json",
        HOL_PARSER_CHINESE_DAYS,
    },
};
#define HOL_SOURCE_COUNT (sizeof(kSources) / sizeof(kSources[0]))

// ---------------------------------------------------------------------------
// Persisted state
// ---------------------------------------------------------------------------
typedef struct {
    int32_t date;      // YYYYMMDD, or 0 when unset
    int8_t  type;      // DAY_* values
} today_t;


typedef struct {
    uint32_t   magic;
    uint16_t   version;
    uint16_t   pad;
    int64_t    today_checked;
    int64_t    year_checked;
    today_t    today;
    hol_year_t years[HOL_SLOTS];
} hol_blob_t;

#define HOL_BLOB_MAGIC   0x484F4C33u   // "HOL3"
#define HOL_BLOB_VERSION 3

#define HOL_REFRESH_INTERVAL_SEC (24 * 3600)        // daily
#define HOL_YEAR_INTERVAL_SEC    (30 * 24 * 3600)   // monthly
#define HOL_RETRY_INTERVAL_SEC   (2 * 3600)         // after a failed attempt

static hol_blob_t s_blob;
static bool       s_loaded = false;

// Set by HOLIDAYS_FORCE_REFRESH in main.cpp to make the first check ignore the
// caches, so a change to the fetch path can be observed without waiting a day. Two
// flags because the daily check consumes its own: the yearly interval would never
// see the force if they shared one.
static bool s_force_today = false;
static bool s_force_year  = false;

void holidays_force_refresh_once(void)
{
    s_force_today = true;
    s_force_year  = true;
}

static void blob_defaults(hol_blob_t *b)
{
    memset(b, 0, sizeof(*b));
    b->magic = HOL_BLOB_MAGIC;
    b->version = HOL_BLOB_VERSION;
    b->today.type = DAY_UNKNOWN;
}

static void blob_load()
{
    if (s_loaded) return;
    blob_defaults(&s_blob);

    // The cache used to live in "dscfg", the namespace the WiFi credentials occupy. It
    // has its own now (store.cpp), so the two can no longer be confused, and a device
    // upgrading in place simply refetches once.
    hol_blob_t tmp;
    if (store_holidays_load(&tmp, sizeof(tmp)) &&
        tmp.magic == HOL_BLOB_MAGIC && tmp.version == HOL_BLOB_VERSION) {
        s_blob = tmp;
    } else {
        ESP_LOGI(TAG, "no usable cached arrangement; using the compiled table");
    }
    s_loaded = true;
}

static void blob_save()
{
    store_holidays_save(&s_blob, sizeof(s_blob));
}

void holidays_runtime_init()
{
    blob_load();
    if (s_blob.today.date != 0 && s_blob.today.type != DAY_UNKNOWN) {
        ESP_LOGI(TAG, "cached today: %ld -> kind %d",
                 (long)s_blob.today.date, (int)s_blob.today.type);
    }
    for (int i = 0; i < HOL_SLOTS; i++) {
        if (s_blob.years[i].year > 0) {
            ESP_LOGI(TAG, "cached year %d: %d holidays, %d makeup",
                     (int)s_blob.years[i].year,
                     (int)s_blob.years[i].holidays.count,
                     (int)s_blob.years[i].makeup.count);
        }
    }
}


// ---------------------------------------------------------------------------
// Lookup
//
// Today's cached verdict wins, because the daily query reflects amendments while the
// yearly table may predate them. Everything else comes from the yearly table and then
// the compiled one.
// ---------------------------------------------------------------------------
static bool list_has(const hol_list_t *l, int m, int d)
{
    for (int i = 0; i < l->count; i++) {
        if (l->month[i] == m && l->day[i] == d) return true;
    }
    return false;
}

static const hol_year_t *year_find(int year)
{
    blob_load();
    for (int i = 0; i < HOL_SLOTS; i++) {
        if (s_blob.years[i].year == year) return &s_blob.years[i];
    }
    return nullptr;
}

static bool compiled_has(int year, int month, int day, bool holidays)
{
    if (year != HOLIDAYS_COMPILED_FIRST_YEAR) return false;
    const holiday_date_t *tab = holidays ? kHolidays2026 : kMakeupWorkdays2026;
    const int n = holidays ? kHolidays2026Count : kMakeupWorkdays2026Count;
    for (int i = 0; i < n; i++) {
        if (tab[i].month == month && tab[i].day == day) return true;
    }
    return false;
}

static int32_t ymd(int y, int m, int d)
{
    return (int32_t)y * 10000 + m * 100 + d;
}

static int today_type_for(int year, int month, int day)
{
    blob_load();
    if (s_blob.today.date != ymd(year, month, day)) return DAY_UNKNOWN;
    return s_blob.today.type;
}

bool holidays_year_supported(int year)
{
    if (year == HOLIDAYS_COMPILED_FIRST_YEAR) return true;
    return year_find(year) != nullptr;
}

bool holidays_is_holiday(int year, int month, int day)
{
    const int t = today_type_for(year, month, day);
    if (t != DAY_UNKNOWN) return t == DAY_HOLIDAY;

    const hol_year_t *y = year_find(year);
    if (y) return list_has(&y->holidays, month, day);
    return compiled_has(year, month, day, true);
}

bool holidays_is_makeup_workday(int year, int month, int day)
{
    const int t = today_type_for(year, month, day);
    if (t != DAY_UNKNOWN) return t == DAY_MAKEUP;

    const hol_year_t *y = year_find(year);
    if (y) return list_has(&y->makeup, month, day);
    return compiled_has(year, month, day, false);
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
#define HOL_RESP_CAP 8192

typedef struct {
    char   buf[HOL_RESP_CAP];
    size_t len;
    bool   overflow;
} hol_resp_t;

static esp_err_t hol_http_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        hol_resp_t *r = (hol_resp_t *)evt->user_data;
        if (!r) return ESP_OK;
        if (r->len + evt->data_len >= HOL_RESP_CAP) {
            r->overflow = true;
            return ESP_OK;
        }
        memcpy(r->buf + r->len, evt->data, evt->data_len);
        r->len += evt->data_len;
        r->buf[r->len] = '\0';
    }
    return ESP_OK;
}

static hol_resp_t *hol_get(const char *url)
{
    hol_resp_t *resp = (hol_resp_t *)malloc(sizeof(hol_resp_t));
    if (!resp) return nullptr;
    memset(resp, 0, sizeof(*resp));

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = 15000;
    cfg.event_handler = hol_http_cb;
    cfg.user_data = resp;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(resp);
        return nullptr;
    }
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0");
    esp_http_client_set_header(client, "Accept", "application/json");

    const esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        // ESP_ERR_HTTP_CONNECT covers both "the name did not resolve" and "the host
        // refused the connection", which need different fixes. Resolve here so the
        // log says which one it was rather than leaving it to guesswork.
        char hostname[64] = {0};
        const char *p = strstr(url, "://");
        p = p ? p + 3 : url;
        size_t n = 0;
        while (p[n] && p[n] != '/' && p[n] != ':' && n < sizeof(hostname) - 1) {
            hostname[n] = p[n];
            n++;
        }
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *res = nullptr;
        const int gai = getaddrinfo(hostname, "443", &hints, &res);
        if (gai == 0 && res) {
            struct sockaddr_in *in = (struct sockaddr_in *)res->ai_addr;
            ESP_LOGW(TAG, "%s -> %s but connect failed (%s)",
                     hostname, inet_ntoa(in->sin_addr), esp_err_to_name(err));
            freeaddrinfo(res);
        } else {
            ESP_LOGW(TAG, "%s did not resolve (gai %d)", hostname, gai);
        }
        esp_http_client_cleanup(client);
        free(resp);
        return nullptr;
    }

    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP %d from %s", status, url);
        free(resp);
        return nullptr;
    }
    if (resp->overflow) {
        ESP_LOGW(TAG, "response exceeded %d bytes", HOL_RESP_CAP);
        free(resp);
        return nullptr;
    }
    return resp;
}

bool holidays_today_stale(int year, int month, int day)
{
    blob_load();
    // One-shot override for bringing the feature up and for support: the caches hold
    // a verdict for a whole day, so without this a fix cannot be observed until
    // tomorrow.
    if (s_force_today) {
        s_force_today = false;
        ESP_LOGW(TAG, "forced daily refresh requested");
        return true;
    }
    if (s_blob.today.date != ymd(year, month, day)) return true;
    if (s_blob.today.type == DAY_UNKNOWN) return true;
    const time_t now = time(nullptr);
    if (now < 1600000000) return false;      // clock not real yet
    if (s_blob.today_checked == 0) return true;
    return (now - s_blob.today_checked) >= HOL_REFRESH_INTERVAL_SEC;
}

bool holidays_fetch_today(int year, int month, int day)
{
    for (size_t i = 0; i < HOL_SOURCE_COUNT; i++) {
        const hol_source_t *s = &kSources[i];
        if (!s->daily_fmt) continue;

        char url[128];
        snprintf(url, sizeof(url), s->daily_fmt, year, month, day);

        hol_resp_t *resp = hol_get(url);
        if (!resp) continue;

        const int kind = holidays_parse_today(resp->buf, resp->len);
        if (kind == DAY_UNKNOWN) ESP_LOGW(TAG, "%s: could not read a day type", s->name);
        free(resp);
        if (kind == DAY_UNKNOWN) continue;

        blob_load();
        s_blob.today.date = ymd(year, month, day);
        s_blob.today.type = (int8_t)kind;
        s_blob.today_checked = (int64_t)time(nullptr);
        blob_save();

        static const char *kinds[] = { "working day", "weekend", "holiday", "makeup day" };
        ESP_LOGI(TAG, "%04d-%02d-%02d is a %s (via %s)",
                 year, month, day, kinds[kind], s->name);
        return true;
    }

    // Every source failed. Record the attempt backdated so the next try comes round
    // in hours, and clear any verdict belonging to another date so nothing stale is
    // applied to today.
    blob_load();
    s_blob.today_checked = (int64_t)time(nullptr) -
                           (HOL_REFRESH_INTERVAL_SEC - HOL_RETRY_INTERVAL_SEC);
    s_blob.today.date = ymd(year, month, day);
    s_blob.today.type = DAY_UNKNOWN;
    blob_save();
    ESP_LOGW(TAG, "no source could answer for %04d-%02d-%02d; using the tables",
             year, month, day);
    return false;
}

// ---------------------------------------------------------------------------
// Yearly table
// ---------------------------------------------------------------------------
bool holidays_year_refresh_due()
{
    blob_load();
    // The force switch covers both intervals: the point of it is to exercise the whole
    // fetch path now rather than in a day or a month.
    if (s_force_year) {
        s_force_year = false;
        ESP_LOGW(TAG, "forced yearly refresh requested");
        return true;
    }
    if (s_blob.year_checked == 0) return true;
    const time_t now = time(nullptr);
    if (now < 1600000000) return false;
    return (now - s_blob.year_checked) >= HOL_YEAR_INTERVAL_SEC;
}

void holidays_mark_year_refreshed()
{
    blob_load();
    s_blob.year_checked = (int64_t)time(nullptr);
    blob_save();
}

bool holidays_runtime_fetch_year(int year)
{
    for (size_t i = 0; i < HOL_SOURCE_COUNT; i++) {
        const hol_source_t *s = &kSources[i];
        if (!s->year_fmt) continue;

        char url[160];
        snprintf(url, sizeof(url), s->year_fmt, year);

        hol_resp_t *resp = hol_get(url);
        if (!resp) continue;

        hol_year_t parsed;
        bool ok = false;
        switch (s->parser) {
            case HOL_PARSER_XIAOAI:
                ok = holidays_parse_year_xiaoai(resp->buf, resp->len, year, &parsed);
                break;
            case HOL_PARSER_CHINESE_DAYS:
                ok = holidays_parse_year_chinese_days(resp->buf, resp->len, year, &parsed);
                break;
            case HOL_PARSER_TIMOR:
                ok = holidays_parse_year_timor(resp->buf, resp->len, year, &parsed);
                break;
            case HOL_PARSER_DAY_ONLY:
            default:
                ok = false;     // the guard above already skips these
                break;
        }
        free(resp);
        if (!ok) {
            ESP_LOGW(TAG, "%s: could not read the %d arrangement", s->name, year);
            continue;
        }

        blob_load();
        if (s_blob.years[0].year == year) {
            s_blob.years[0] = parsed;
        } else {
            for (int k = HOL_SLOTS - 1; k > 0; k--) {
                s_blob.years[k] = s_blob.years[k - 1];
            }
            s_blob.years[0] = parsed;
        }
        s_blob.year_checked = (int64_t)time(nullptr);
        blob_save();
        ESP_LOGI(TAG, "stored %d: %d holidays, %d makeup days (via %s)",
                 year, (int)parsed.holidays.count, (int)parsed.makeup.count, s->name);
        return true;
    }

    holidays_mark_year_refreshed();
    ESP_LOGW(TAG, "no source returned the %d arrangement; the compiled table stands",
             year);
    return false;
}

// ---------------------------------------------------------------------------
// Background refresh
//
// Two requests on different schedules, because they answer different questions:
//
//   daily   - the ~200 byte single-day endpoint, for the day actually being
//             displayed. This is what keeps up with an arrangement that is amended
//             after it is published, which is the point of checking at all.
//   monthly - the ~3 KB yearly table, so days the daily check cannot see - the next
//             switch time can fall on one - are still covered.
//
// Both write to NVS and both fall back to the compiled table, so no failure here can
// change what the panel shows; it only stops the board from learning.
//
// Its own task because each request is an HTTPS round trip and the main loop must not
// block on it. 12 KB of stack for the TLS handshake, same as the balance poll.
// ---------------------------------------------------------------------------
static void refresh_task(void *)
{
    while (true) {
        if (wifi_get_status().connected && clock_is_synced()) {
            struct tm bj;
            pricing_beijing_tm(clock_now(), &bj);
            const int year = bj.tm_year + 1900;
            const int month = bj.tm_mon + 1;
            const int day = bj.tm_mday;

            if (holidays_today_stale(year, month, day)) {
                holidays_fetch_today(year, month, day);
            }
            // A year the compiled table does not cover has to come from here, so fetch
            // it even when the monthly interval has not elapsed yet.
            if (holidays_year_refresh_due() || !holidays_year_supported(year)) {
                holidays_runtime_fetch_year(year);
            }
        }
        // Woken hourly so a board whose clock only just synced, or whose network was
        // down, retries promptly rather than waiting a whole day. Both fetches are
        // gated on their own intervals inside.
        vTaskDelay(pdMS_TO_TICKS(60 * 60 * 1000));
    }
}

void holidays_start_refresh_task()
{
    xTaskCreatePinnedToCore(refresh_task, "holidays", 12288, nullptr, 2, nullptr, 0);
}
