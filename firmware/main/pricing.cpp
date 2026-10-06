#include "pricing.h"
#include "holidays.h"

#include <stdio.h>
#include <string.h>

// Beijing time is UTC+8 with no daylight saving.
#define BEIJING_OFFSET_SEC (8 * 3600)

// ---------------------------------------------------------------------------
// Official DeepSeek price table, CNY per 1M tokens.
// Peak values are published; off-peak is exactly half.
// ---------------------------------------------------------------------------
static const price_pair_t kTable[2][3] = {
    // deepseek-flash
    {
        /* cache hit  */ {0.04f, 0.02f},
        /* cache miss */ {2.00f, 1.00f},
        /* output     */ {8.00f, 4.00f},
    },
    // deepseek-v4-pro
    {
        /* cache hit  */ {0.30f,  0.15f},
        /* cache miss */ {9.00f,  4.50f},
        /* output     */ {27.00f, 13.50f},
    },
};

price_pair_t pricing_lookup(price_model_t model, price_tier_t tier)
{
    if (model < 0 || model > 1) model = PM_FLASH;
    if (tier  < 0 || tier  > 2) tier  = PT_CACHE_HIT;
    return kTable[model][tier];
}

void pricing_beijing_tm(time_t utc, struct tm *out)
{
    time_t bj = utc + BEIJING_OFFSET_SEC;
    gmtime_r(&bj, out);
}

// Peak windows, minutes since midnight Beijing time.
static bool in_peak_window(int hour, int minute)
{
    const int m = hour * 60 + minute;
    return (m >= 9 * 60 && m < 12 * 60) ||   // 09:00-12:00
           (m >= 14 * 60 && m < 18 * 60);    // 14:00-18:00
}

bool pricing_is_holiday(int year, int month, int day)
{
    // A compensatory workday is never a holiday, even when it falls inside a
    // holiday block.
    if (holidays_is_makeup_workday(year, month, day)) return false;
    return holidays_is_holiday(year, month, day);
}

bool pricing_is_makeup_workday(int year, int month, int day)
{
    return holidays_is_makeup_workday(year, month, day);
}

price_state_t pricing_classify(time_t utc)
{
    struct tm bj;
    pricing_beijing_tm(utc, &bj);

    const int year  = bj.tm_year + 1900;
    const int month = bj.tm_mon + 1;
    const int day   = bj.tm_mday;

    // Statutory holidays: off-peak all day.
    if (pricing_is_holiday(year, month, day)) return PRICE_OFFPEAK;

    // Weekends are off-peak unless the government made them working days.
    const bool weekend = (bj.tm_wday == 0 || bj.tm_wday == 6);
    if (weekend && !pricing_is_makeup_workday(year, month, day)) {
        return PRICE_OFFPEAK;
    }

    return in_peak_window(bj.tm_hour, bj.tm_min) ? PRICE_PEAK : PRICE_OFFPEAK;
}

const char *pricing_state_name(price_state_t st)
{
    return (st == PRICE_PEAK) ? "PEAK" : "OFF-PEAK";
}

int pricing_current_window_text(time_t utc, char *out, size_t out_len)
{
    struct tm bj;
    pricing_beijing_tm(utc, &bj);
    const int year  = bj.tm_year + 1900;
    const int month = bj.tm_mon + 1;
    const int day   = bj.tm_mday;

    const char *text;
    if (pricing_is_holiday(year, month, day)) {
        text = "HOLIDAY";
    } else if ((bj.tm_wday == 0 || bj.tm_wday == 6) &&
               !pricing_is_makeup_workday(year, month, day)) {
        text = "WEEKEND";
    } else {
        const int m = bj.tm_hour * 60 + bj.tm_min;
        if (m >= 9 * 60 && m < 12 * 60)       text = "09:00-12:00";
        else if (m >= 14 * 60 && m < 18 * 60) text = "14:00-18:00";
        else                                  text = "OFF-PEAK";
    }

    const int n = snprintf(out, out_len, "%s", text);
    return (n < 0) ? 0 : n;
}

// Advance one minute and ask again, up to a full week.
//
// The window has to be a week, not a day. From Saturday morning until the next
// Monday 09:00 the state is OFF-PEAK throughout, so a 24-hour lookahead finds no
// transition at all and the old code returned 0 - which the panel then rendered as
// a switch happening right now. The schedule repeats weekly, so a week always
// contains a transition and 10080 iterations is negligible on demand.
int64_t pricing_seconds_to_next_change(time_t utc)
{
    const price_state_t now = pricing_classify(utc);

    // Start at the next whole minute so we do not report 0 when we are one
    // second away from a boundary.
    time_t probe = utc - (utc % 60) + 60;

    for (int i = 0; i < 7 * 24 * 60 + 2; i++) {
        if (pricing_classify(probe) != now) {
            return (int64_t)(probe - utc);
        }
        probe += 60;
    }
    return 0;
}
