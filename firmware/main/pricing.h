#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Peak / off-peak classification, per the official DeepSeek pricing rules:
//
//   Peak    : Monday-Friday 09:00-12:00 and 14:00-18:00 Beijing time,
//             excluding Chinese statutory holidays.
//   Off-peak: everything else, including weekends and holidays all day.
//
//   Off-peak prices are exactly half of peak prices.
//
// All judgement is made in Beijing time (UTC+8) regardless of the device's
// configured timezone, because the published windows are Beijing time.
// ---------------------------------------------------------------------------

typedef enum {
    PRICE_PEAK    = 0,
    PRICE_OFFPEAK = 1,
} price_state_t;

// Model + billing component selection (mirrors app_config.h enums).
typedef enum {
    PM_FLASH = 0,
    PM_PRO   = 1,
} price_model_t;

typedef enum {
    PT_CACHE_HIT  = 0,
    PT_CACHE_MISS = 1,
    PT_OUTPUT     = 2,
} price_tier_t;

// Price table, CNY per 1M tokens, peak values (off-peak = peak / 2).
typedef struct {
    float peak;
    float offpeak;
} price_pair_t;

// Look up the price pair for a model/tier combination.
price_pair_t pricing_lookup(price_model_t model, price_tier_t tier);

// Classify an instant. `utc` is evaluated as Beijing time internally.
price_state_t pricing_classify(time_t utc);

// Human-readable state name ("PEAK" / "OFFPEAK").
const char *pricing_state_name(price_state_t st);

// True when the given Beijing calendar date is a statutory holiday.
bool pricing_is_holiday(int year, int month, int day);

// True when the given Beijing calendar date is a compensatory workday
// (a weekend that the government designates as a working day).
bool pricing_is_makeup_workday(int year, int month, int day);

// Format the window currently in force, e.g. "09:00-12:00" or "OFF-PEAK".
// Returns the number of characters written (excluding the terminator).
int pricing_current_window_text(time_t utc, char *out, size_t out_len);

// Seconds until the next peak/off-peak boundary. Never returns a negative
// value; returns 0 only if the whole calendar is exhausted (impossible).
int64_t pricing_seconds_to_next_change(time_t utc);

// Convert UTC to Beijing broken-down time.
void pricing_beijing_tm(time_t utc, struct tm *out);
