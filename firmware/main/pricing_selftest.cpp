// ---------------------------------------------------------------------------
// Offline check of the tariff rules.
//
// The NEXT line on the panel is only as good as pricing_seconds_to_next_change(),
// which walks forward a minute at a time until the state flips. This sweeps a full
// week hour by hour, prints the state and the next switch for each hour, and
// checks that the value it returns really does land on a state change.
//
// Beijing time: peak is Mon-Fri 09:00-12:00 and 14:00-18:00, off-peak is
// everything else including the lunch break and the whole weekend.
// ---------------------------------------------------------------------------
#include "pricing.h"
#include "pricing_selftest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <esp_log.h>

static const char *TAG = "pricetest";

static const char *wday_name(int wday)
{
    static const char *names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    return (wday >= 0 && wday < 7) ? names[wday] : "???";
}

static time_t beijing_to_utc(int year, int mon, int day, int hour, int min)
{
    // Never build this with mktime(): the panel's convention is that a time_t is
    // UTC and pricing_beijing_tm() adds the +8 offset. mktime() interprets its
    // argument in the process's local zone, which shifted every test case by an
    // hour and made 39 of 192 samples look wrong while the rules were correct.
    // Building the UTC instant explicitly keeps the two conventions aligned.
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = day;
    // Beijing is UTC+8, with no daylight saving.
    t.tm_hour = hour - 8;
    t.tm_min  = min;
    t.tm_isdst = 0;
    return timegm(&t);
}

void pricing_selftest()
{
    // 2026-05-18 is a Monday.
    const int days = 8;
    int checked = 0, bad = 0;

    ESP_LOGI(TAG, "sweeping %d days from 2026-05-18 00:00 Beijing", days);
    ESP_LOGI(TAG, "  day  time   state    next switch        delta   ok");

    for (int d = 0; d < days; d++) {
        for (int h = 0; h < 24; h++) {
            const time_t utc = beijing_to_utc(2026, 5, 18 + d, h, 0);
            const price_state_t st = pricing_classify(utc);
            const int64_t secs = pricing_seconds_to_next_change(utc);

            struct tm bj_now, bj_next;
            pricing_beijing_tm(utc, &bj_now);
            pricing_beijing_tm(utc + secs, &bj_next);

            // The returned instant must actually be a different state.
            const price_state_t after = pricing_classify(utc + secs);
            const bool ok = (secs > 0) && (after != st);
            if (!ok) bad++;
            checked++;

            // Print a compact line only around interesting hours to keep the log
            // readable: midnight, the window edges, and the weekend.
            const bool interesting = (h == 0) || (h == 8) || (h == 9) || (h == 11) ||
                                     (h == 12) || (h == 13) || (h == 14) ||
                                     (h == 17) || (h == 18) || (h == 19);
            if (interesting || !ok) {
                ESP_LOGI(TAG, "  %s %02d:00  %-8s -> %02d-%02d %02d:%02d  %6lldm  %s",
                         wday_name(bj_now.tm_wday), h,
                         pricing_state_name(st),
                         bj_next.tm_mon + 1, bj_next.tm_mday,
                         bj_next.tm_hour, bj_next.tm_min,
                         (long long)(secs / 60),
                         ok ? "ok" : "BAD");
            }
        }
    }

    ESP_LOGI(TAG, "sweep done: %d checked, %d bad", checked, bad);

    // Specific expectations, stated so a regression is obvious.
    struct { int d, h, m; price_state_t want; const char *why; } cases[] = {
        { 18,  8, 59, PRICE_OFFPEAK, "just before the morning window" },
        { 18,  9,  0, PRICE_PEAK,    "morning window opens" },
        { 18, 11, 59, PRICE_PEAK,    "end of morning window" },
        { 18, 12,  0, PRICE_OFFPEAK, "lunch break" },
        { 18, 14,  0, PRICE_PEAK,    "afternoon window opens" },
        { 18, 17, 59, PRICE_PEAK,    "end of afternoon window" },
        { 18, 18,  0, PRICE_OFFPEAK, "evening" },
        { 22, 10,  0, PRICE_PEAK,    "Friday morning is still peak" },
        { 23, 10,  0, PRICE_OFFPEAK, "Saturday" },
        { 24, 10,  0, PRICE_OFFPEAK, "Sunday" },
        { 25, 10,  0, PRICE_PEAK,    "the next Monday is peak again" },
    };
    for (const auto &c : cases) {
        const time_t utc = beijing_to_utc(2026, 5, c.d, c.h, c.m);
        const price_state_t got = pricing_classify(utc);
        ESP_LOGI(TAG, "  expect %02d %02d:%02d %-8s -> %-8s %s",
                 c.d, c.h, c.m, pricing_state_name(c.want),
                 pricing_state_name(got), got == c.want ? "ok" : "MISMATCH");
    }
}
