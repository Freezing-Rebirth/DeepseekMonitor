#include "holidays.h"
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
#include <nvs.h>
#include <cJSON.h>

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

// Normalised day kinds. Deliberately the numbering timor.tech and xiaoai.me already
// use, so only dreace's text form needs translating.
#define DAY_WORKDAY  0
#define DAY_WEEKEND  1
#define DAY_HOLIDAY  2
#define DAY_MAKEUP   3
#define DAY_UNKNOWN -1

typedef struct {
    const char *name;
    const char *daily_fmt;   // %04d-%02d-%02d
    const char *year_fmt;    // %d; null for a day-only service
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
    },
    {
        "timor.tech",
        "https://timor.tech/api/holiday/info/%04d-%02d-%02d",
        "https://timor.tech/api/holiday/year/%d",
    },
    {
        "xiaoai.me",
        "https://publicapi.xiaoai.me/holiday/day?date=%04d-%02d-%02d",
        "https://publicapi.xiaoai.me/holiday/year?date=%d",
    },
    {
        "chinese-days/jsdelivr",
        nullptr,
        "https://cdn.jsdelivr.net/npm/chinese-days/dist/years/%d.json",
    },
    {
        "chinese-days/fastly",
        nullptr,
        "https://fastly.jsdelivr.net/npm/chinese-days/dist/years/%d.json",
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

#define HOL_SLOTS        3
#define HOL_MAX_DATES   48      // 2026 has 33 holidays; leave room
#define MK_MAX_DATES    24      // 2026 has 6

typedef struct {
    int16_t count;
    uint8_t month[HOL_MAX_DATES];
    uint8_t day[HOL_MAX_DATES];
} hol_list_t;

typedef struct {
    int16_t year;
    hol_list_t holidays;
    hol_list_t makeup;
} hol_year_t;

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
#define HOL_NVS_NS       "dscfg"
#define HOL_NVS_KEY      "holidays"

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

    nvs_handle_t h;
    if (nvs_open(HOL_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        hol_blob_t tmp;
        size_t len = sizeof(tmp);
        if (nvs_get_blob(h, HOL_NVS_KEY, &tmp, &len) == ESP_OK &&
            tmp.magic == HOL_BLOB_MAGIC && tmp.version == HOL_BLOB_VERSION) {
            s_blob = tmp;
        } else {
            ESP_LOGI(TAG, "no usable cached arrangement; using the compiled table");
        }
        nvs_close(h);
    }
    s_loaded = true;
}

static void blob_save()
{
    nvs_handle_t h;
    if (nvs_open(HOL_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, HOL_NVS_KEY, &s_blob, sizeof(s_blob));
    nvs_commit(h);
    nvs_close(h);
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

int holidays_runtime_year(int slot)
{
    blob_load();
    if (slot < 0 || slot >= HOL_SLOTS) return 0;
    return s_blob.years[slot].year;
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

// ---------------------------------------------------------------------------
// Daily check: three response shapes, one normalised answer
// ---------------------------------------------------------------------------
static int map_text_kind(const char *s)
{
    if (!s) return DAY_UNKNOWN;
    // dreace answers in Chinese: 假日 / 调休 / 周末 / 工作日.
    if (strcmp(s, "\xE5\x81\x87\xE6\x97\xA5") == 0 || strcmp(s, "holiday") == 0)
        return DAY_HOLIDAY;
    if (strcmp(s, "\xE8\xB0\x83\xE4\xBC\x91") == 0 || strcmp(s, "makeup") == 0)
        return DAY_MAKEUP;
    if (strcmp(s, "\xE5\x91\xA8\xE6\x9C\xAB") == 0 || strcmp(s, "weekend") == 0)
        return DAY_WEEKEND;
    if (strcmp(s, "\xE5\xB7\xA5\xE4\xBD\x9C\xE6\x97\xA5") == 0 || strcmp(s, "workday") == 0)
        return DAY_WORKDAY;
    return DAY_UNKNOWN;
}

static int parse_today(const char *json, size_t len, const char *source)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        ESP_LOGW(TAG, "%s: response was not valid JSON", source);
        return DAY_UNKNOWN;
    }

    int kind = DAY_UNKNOWN;

    // timor.tech: {"code":0,"type":{"type":2,"name":"国庆节"}}
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsObject(type)) {
        const cJSON *tnum = cJSON_GetObjectItemCaseSensitive(type, "type");
        if (cJSON_IsNumber(tnum)) {
            const int v = tnum->valueint;
            if (v >= 0 && v <= 3) kind = v;
        }
    }

    // dreace: {"isHoliday":true,"type":"假日"} - here type is a string.
    if (kind == DAY_UNKNOWN && cJSON_IsString(type)) {
        kind = map_text_kind(type->valuestring);
    }
    if (kind == DAY_UNKNOWN) {
        const cJSON *is_hol = cJSON_GetObjectItemCaseSensitive(root, "isHoliday");
        if (cJSON_IsBool(is_hol)) {
            kind = cJSON_IsTrue(is_hol) ? DAY_HOLIDAY : DAY_WORKDAY;
        }
    }

    // xiaoai.me: {"code":0,"data":[{"daytype":1,...}]}
    if (kind == DAY_UNKNOWN) {
        const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
        const cJSON *first = cJSON_IsArray(data) ? cJSON_GetArrayItem(data, 0) : nullptr;
        if (cJSON_IsObject(first)) {
            const cJSON *dt = cJSON_GetObjectItemCaseSensitive(first, "daytype");
            if (cJSON_IsNumber(dt)) {
                const int v = dt->valueint;
                if (v >= 0 && v <= 3) kind = v;
            }
        }
    }

    cJSON_Delete(root);

    if (kind == DAY_UNKNOWN) {
        ESP_LOGW(TAG, "%s: could not read a day type from the response", source);
    }
    return kind;
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

        const int kind = parse_today(resp->buf, resp->len, s->name);
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

static bool parse_mmdd(const char *s, int *m, int *d)
{
    if (!s || strlen(s) < 5) return false;
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') return false;
    if (s[2] != '-') return false;
    if (s[3] < '0' || s[3] > '9' || s[4] < '0' || s[4] > '9') return false;
    *m = (s[0] - '0') * 10 + (s[1] - '0');
    *d = (s[3] - '0') * 10 + (s[4] - '0');
    return (*m >= 1 && *m <= 12 && *d >= 1 && *d <= 31);
}

static void year_add(hol_year_t *y, bool holiday, int m, int d)
{
    hol_list_t *dst = holiday ? &y->holidays : &y->makeup;
    const int cap = holiday ? HOL_MAX_DATES : MK_MAX_DATES;
    if (dst->count >= cap) return;
    dst->month[dst->count] = (uint8_t)m;
    dst->day[dst->count] = (uint8_t)d;
    dst->count++;
}

// "YYYY-MM-DD" -> month, day
static void ymd_from_iso(const char *s, int *m, int *d)
{
    *m = (s[5] - '0') * 10 + (s[6] - '0');
    *d = (s[8] - '0') * 10 + (s[9] - '0');
}

// timor: {"code":0,"holiday":{"01-01":{"holiday":true,"date":"2026-01-01"},...}}
static bool parse_year_timor(const char *json, size_t len, int want, hol_year_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;

    const cJSON *hol = cJSON_GetObjectItemCaseSensitive(root, "holiday");
    if (!cJSON_IsObject(hol)) {
        cJSON_Delete(root);
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->year = (int16_t)want;

    int dated = 0;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, hol) {
        if (!cJSON_IsObject(item)) continue;
        int m = 0, d = 0;
        if (!parse_mmdd(item->string, &m, &d)) continue;

        const cJSON *date = cJSON_GetObjectItemCaseSensitive(item, "date");
        if (cJSON_IsString(date) && strlen(date->valuestring) >= 4) {
            // Reject a response for the wrong year rather than trusting it: the API
            // echoes the full date, so the year can be checked.
            if (atoi(date->valuestring) != want) {
                cJSON_Delete(root);
                return false;
            }
            dated++;
        }
        year_add(out, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "holiday")), m, d);
    }
    cJSON_Delete(root);
    return dated > 0 && out->holidays.count > 0;
}

// xiaoai: {"code":0,"data":[{"daytype":1,"date":"2026-01-01"},...]}
static bool parse_year_xiaoai(const char *json, size_t len, int want, hol_year_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;

    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->year = (int16_t)want;

    int dated = 0;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, data) {
        const cJSON *date = cJSON_GetObjectItemCaseSensitive(item, "date");
        const cJSON *dt = cJSON_GetObjectItemCaseSensitive(item, "daytype");
        if (!cJSON_IsString(date) || !cJSON_IsNumber(dt)) continue;
        if (strlen(date->valuestring) < 10) continue;
        if (atoi(date->valuestring) != want) continue;
        dated++;

        // daytype: 1 holiday, 3 compensatory workday. 0 and 2 are ordinary days.
        const int v = dt->valueint;
        if (v == 1 || v == 3) {
            int m = 0, d = 0;
            ymd_from_iso(date->valuestring, &m, &d);
            year_add(out, v == 1, m, d);
        }
    }
    cJSON_Delete(root);
    return dated > 0 && out->holidays.count > 0;
}

// chinese-days: {"holidays":{"2026-01-01":"New Year's Day,元旦,1",...},
//                "workdays":{"2026-01-04":"...",...}}
//
// A static release rather than a query API: the keys are full ISO dates and the two
// objects are exactly the two lists this module wants, so nothing inside the values
// has to be interpreted.
static bool parse_year_chinese_days(const char *json, size_t len, int want,
                                    hol_year_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;

    const cJSON *holidays = cJSON_GetObjectItemCaseSensitive(root, "holidays");
    const cJSON *workdays = cJSON_GetObjectItemCaseSensitive(root, "workdays");
    if (!cJSON_IsObject(holidays)) {
        cJSON_Delete(root);
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->year = (int16_t)want;

    int dated = 0;
    // Two passes over the same shape; only the destination list differs.
    for (int pass = 0; pass < 2; pass++) {
        const cJSON *src = pass == 0 ? holidays : workdays;
        if (!cJSON_IsObject(src)) continue;

        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, src) {
            const char *key = item->string;
            if (!key || strlen(key) < 10) continue;
            if (atoi(key) != want) continue;

            // "YYYY-MM-DD"
            const int m = (key[5] - '0') * 10 + (key[6] - '0');
            const int d = (key[8] - '0') * 10 + (key[9] - '0');
            if (m < 1 || m > 12 || d < 1 || d > 31) continue;

            dated++;
            year_add(out, pass == 0, m, d);
        }
    }
    cJSON_Delete(root);
    return dated > 0 && out->holidays.count > 0;
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
        bool ok;
        if (strcmp(s->name, "xiaoai.me") == 0) {
            ok = parse_year_xiaoai(resp->buf, resp->len, year, &parsed);
        } else if (strncmp(s->name, "chinese-days", 12) == 0) {
            ok = parse_year_chinese_days(resp->buf, resp->len, year, &parsed);
        } else {
            ok = parse_year_timor(resp->buf, resp->len, year, &parsed);
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
