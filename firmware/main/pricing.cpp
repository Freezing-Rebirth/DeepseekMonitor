#include "pricing.h"
#include "pricing_rules.h"

#include <stdio.h>
#include <string.h>

#include "holidays.h"

// The arithmetic lives in pricing_rules.cpp, which has no ESP-IDF dependency and is
// tested on the host. This file is the adapter: it supplies the calendar facts from
// the holiday cache and maps the result onto the public enum.

// ---------------------------------------------------------------------------
// DeepSeek price table, CNY per 1M tokens.
// Peak values are published; off-peak is exactly half.
//
// The one exception is the cache-hit row for deepseek-flash. That rate is what the
// panel derives both the token equivalent and the BASIS line from, and the figure in
// use is a conversion done by hand rather than a published number: 0.12 CNY per 1M
// tokens at peak, 0.06 off-peak. Raising it lowers the token equivalent by the same
// factor, since the two are the same rate.
// ---------------------------------------------------------------------------
static const price_pair_t kTable[2][3] = {
    // deepseek-flash
    {
        /* cache hit  */ {0.12f, 0.06f},
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
    pricing_rules_beijing_tm(utc, out);
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

// The calendar facts for one Beijing date. This and calendar_cb below are the only
// places the tariff rules touch the holiday subsystem.
static pricing_calendar_t calendar_for(const struct tm *bj)
{
    pricing_calendar_t c = {false, false};
    const int year  = bj->tm_year + 1900;
    const int month = bj->tm_mon + 1;
    const int day   = bj->tm_mday;
    c.is_makeup_workday = pricing_is_makeup_workday(year, month, day);
    c.is_holiday        = pricing_is_holiday(year, month, day);
    return c;
}

static pricing_calendar_t calendar_cb(const struct tm *bj, void *ctx)
{
    (void)ctx;
    return calendar_for(bj);
}

price_state_t pricing_classify(time_t utc)
{
    struct tm bj;
    pricing_beijing_tm(utc, &bj);
    return (pricing_rules_classify(&bj, calendar_for(&bj)) == PRICING_WINDOW_PEAK)
               ? PRICE_PEAK
               : PRICE_OFFPEAK;
}

const char *pricing_state_name(price_state_t st)
{
    return (st == PRICE_PEAK) ? "PEAK" : "OFF-PEAK";
}

int64_t pricing_seconds_to_next_change(time_t utc)
{
    return pricing_rules_seconds_to_next_change(utc, calendar_cb, nullptr);
}
