#pragma once

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Chinese statutory holidays and compensatory workdays.
//
// DeepSeek bills peak rates Monday-Friday 09:00-12:00 and 14:00-18:00 Beijing
// time, excluding statutory holidays. How a given day is actually billed
// therefore depends on the State Council's yearly arrangement, which is two lists:
// days that are holidays despite falling on a weekday, and weekend days that are
// designated working days.
//
// Two sources, in priority order:
//
//   1. A runtime table fetched from timor.tech's holiday API and persisted in NVS.
//      Replaced monthly, so the arrangement follows the published one without a
//      reflash. This is the only source that can cover a year the firmware predates.
//   2. A compiled-in table. Always present, so the board is correct offline, on
//      first boot, and if the API is unreachable or changes shape.
//
// Nothing here can take the board offline or make it display a wrong number on
// failure: a bad response is rejected and the compiled table stands.
// ---------------------------------------------------------------------------

// Oldest year the compiled table covers.
#define HOLIDAYS_COMPILED_FIRST_YEAR 2026

typedef struct {
    int year;
    int month;   // 1-12
    int day;     // 1-31
} holiday_date_t;

// Statutory holidays for 2026 (full days off, treated as off-peak by DeepSeek).
extern const holiday_date_t kHolidays2026[];
extern const int kHolidays2026Count;

// Compensatory workdays for 2026: weekend days the government turns into working
// days. DeepSeek's peak windows apply on these, so they must be treated as ordinary
// weekdays rather than weekends.
extern const holiday_date_t kMakeupWorkdays2026[];
extern const int kMakeupWorkdays2026Count;

// Lookup helpers.
bool holidays_is_holiday(int year, int month, int day);
bool holidays_is_makeup_workday(int year, int month, int day);

// True when either source can answer for this year. False means the panel is
// falling back to plain weekday rules, so holidays and makeup days are not being
// applied at all.
bool holidays_year_supported(int year);

// Load what is cached in NVS. Safe before the network is up.
void holidays_runtime_init();

// ---------------------------------------------------------------------------
// Daily check
//
// https://timor.tech/api/holiday/info/<YYYY-MM-DD> answers for one day and returns
// roughly 200 bytes. Its `type.type` field is the whole answer:
//
//   0  ordinary working day      2  statutory holiday
//   1  ordinary weekend          3  compensatory workday
//
// This is the authoritative source for *today* and is cheap enough to poll daily.
// It cannot answer for a future day, which is what the yearly table below is for.
// ---------------------------------------------------------------------------
bool holidays_fetch_today(int year, int month, int day);

// True when today's cached check is stale (never done, or done on an earlier day).
bool holidays_today_stale(int year, int month, int day);

// ---------------------------------------------------------------------------
// Yearly table
//
// A full-year fetch is ~3 KB, so it is only used to cover days the daily check
// cannot see. Refreshed monthly, and used as a fallback if the daily check fails.
// ---------------------------------------------------------------------------
bool holidays_runtime_fetch_year(int year);
bool holidays_year_refresh_due();
void holidays_mark_year_refreshed();

// Days currently held in the runtime year table, for logging.
int holidays_runtime_year(int slot);   // slot 0 = newest

// Make the next daily check ignore the caches. For bringing the feature up and for
// support: a cached verdict covers a whole day, so a fix is otherwise unobservable
// until tomorrow. Driven by HOLIDAYS_FORCE_REFRESH in main.cpp.
void holidays_force_refresh_once(void);

