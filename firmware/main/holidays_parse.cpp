// The holiday APIs' response shapes. See holidays_parse.h.

#include "holidays_parse.h"

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

// ---------------------------------------------------------------------------
// Daily check: four response shapes, one normalised answer
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

int holidays_parse_today(const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return DAY_UNKNOWN;

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
    return kind;
}

// ---------------------------------------------------------------------------
// Yearly table
// ---------------------------------------------------------------------------
static void year_add(hol_year_t *y, bool holiday, int m, int d)
{
    hol_list_t *dst = holiday ? &y->holidays : &y->makeup;
    const int cap = holiday ? HOL_MAX_DATES : MK_MAX_DATES;
    if (dst->count >= cap) return;
    dst->month[dst->count] = (uint8_t)m;
    dst->day[dst->count] = (uint8_t)d;
    dst->count++;
}

// "MM-DD" -> month, day
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

// "YYYY-MM-DD" -> month, day
static void ymd_from_iso(const char *s, int *m, int *d)
{
    *m = (s[5] - '0') * 10 + (s[6] - '0');
    *d = (s[8] - '0') * 10 + (s[9] - '0');
}

// timor: {"code":0,"holiday":{"01-01":{"holiday":true,"date":"2026-01-01"},...}}
bool holidays_parse_year_timor(const char *json, size_t len, int year, hol_year_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;

    const cJSON *hol = cJSON_GetObjectItemCaseSensitive(root, "holiday");
    if (!cJSON_IsObject(hol)) {
        cJSON_Delete(root);
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->year = (int16_t)year;

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
            if (atoi(date->valuestring) != year) {
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
bool holidays_parse_year_xiaoai(const char *json, size_t len, int year, hol_year_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;

    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->year = (int16_t)year;

    int dated = 0;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, data) {
        const cJSON *date = cJSON_GetObjectItemCaseSensitive(item, "date");
        const cJSON *dt = cJSON_GetObjectItemCaseSensitive(item, "daytype");
        if (!cJSON_IsString(date) || !cJSON_IsNumber(dt)) continue;
        if (strlen(date->valuestring) < 10) continue;
        if (atoi(date->valuestring) != year) continue;
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
// objects are exactly the two lists this module wants, so nothing inside the values has to
// be interpreted.
bool holidays_parse_year_chinese_days(const char *json, size_t len, int year, hol_year_t *out)
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
    out->year = (int16_t)year;

    int dated = 0;
    // Two passes over the same shape; only the destination list differs.
    for (int pass = 0; pass < 2; pass++) {
        const cJSON *src = pass == 0 ? holidays : workdays;
        if (!cJSON_IsObject(src)) continue;

        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, src) {
            const char *key = item->string;
            if (!key || strlen(key) < 10) continue;
            if (atoi(key) != year) continue;

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
