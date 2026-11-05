#pragma once

// Tariff arithmetic, free of every ESP-IDF dependency.
//
// This file exists so the peak/off-peak rules can be compiled and tested on a
// workstation. It deliberately knows nothing about NVS, the network or the holiday
// cache: the calendar arrives as two booleans, so a test can drive any date it likes
// and pricing.cpp stays the only place that talks to holidays.
//
// Pure: no allocation, no globals, no clock reads.

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Beijing time is UTC+8 with no daylight saving.
#define PRICING_BEIJING_OFFSET_SEC (8 * 3600)

typedef enum {
    PRICING_WINDOW_PEAK = 0,
    PRICING_WINDOW_OFFPEAK = 1,
} pricing_window_t;

// What the calendar says about one Beijing date.
typedef struct {
    bool is_holiday;         // statutory holiday: off-peak all day
    bool is_makeup_workday;  // a weekend or holiday block made into a working day
} pricing_calendar_t;

// Answers the calendar for an instant, so the week-long lookahead below can ask about
// days beyond the one being displayed.
typedef pricing_calendar_t (*pricing_calendar_fn)(const struct tm *beijing, void *ctx);

// UTC to Beijing civil time.
void pricing_rules_beijing_tm(time_t utc, struct tm *out);

// True when the Beijing wall clock falls inside a published peak window:
// 09:00-12:00 or 14:00-18:00.
bool pricing_rules_in_peak(int hour, int minute);

// The tariff at one Beijing instant.
pricing_window_t pricing_rules_classify(const struct tm *beijing, pricing_calendar_t calendar);

// Seconds until the state changes.
//
// The lookahead is a full week, not a day: from Saturday morning until Monday 09:00 the
// state is OFF-PEAK throughout, so a 24-hour window finds no transition and used to
// return 0, which the panel rendered as a switch happening right now. Returns 0 only if
// the state never changes within a week, which cannot happen with a weekly schedule.
int64_t pricing_rules_seconds_to_next_change(time_t utc, pricing_calendar_fn calendar, void *ctx);
