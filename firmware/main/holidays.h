#pragma once

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Chinese statutory holidays and compensatory workdays.
//
// One table per year. Add a new file when the State Council publishes the
// next year's arrangement; nothing else in the firmware needs to change.
// ---------------------------------------------------------------------------

typedef struct {
    int year;
    int month;   // 1-12
    int day;     // 1-31
} holiday_date_t;

// Statutory holidays (full days off, treated as off-peak by DeepSeek).
extern const holiday_date_t kHolidays2026[];
extern const int kHolidays2026Count;

// Compensatory workdays: weekend days that the government turns into working
// days. DeepSeek's peak windows apply on these days, so they must be treated
// as ordinary weekdays rather than weekends.
extern const holiday_date_t kMakeupWorkdays2026[];
extern const int kMakeupWorkdays2026Count;

// Lookup helpers. Return false when the year has no table at all.
bool holidays_is_holiday(int year, int month, int day);
bool holidays_is_makeup_workday(int year, int month, int day);
bool holidays_year_supported(int year);
