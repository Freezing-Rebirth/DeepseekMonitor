#pragma once

// Reading the holiday APIs' response shapes.
//
// Pure apart from cJSON: no HTTP, no NVS, no clock, no logging. holidays_net.cpp fetches
// the bytes and writes the cache; this decides what they mean, and is tested on the host
// against payloads in each of the four shapes the firmware accepts.
//
// That separation matters more here than elsewhere in the project: a parser that quietly
// returns the wrong list does not fail, it makes the device bill peak rates on a holiday
// for as long as the cache lives.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Normalised day kinds. Deliberately the numbering timor.tech and xiaoai.me already use,
// so only dreace's text form needs translating.
#define DAY_WORKDAY  0
#define DAY_WEEKEND  1
#define DAY_HOLIDAY  2
#define DAY_MAKEUP   3
#define DAY_UNKNOWN -1

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

// One day -> a DAY_* kind, whichever of the four shapes the body uses. DAY_UNKNOWN when
// nothing in it can be read.
int holidays_parse_today(const char *json, size_t len);

// A whole year -> the two date lists. False when the body is not that source's shape, is
// for the wrong year, or yields no holidays at all (an empty table would silently mean
// "every day is a working day").
bool holidays_parse_year_timor(const char *json, size_t len, int year, hol_year_t *out);
bool holidays_parse_year_xiaoai(const char *json, size_t len, int year, hol_year_t *out);
bool holidays_parse_year_chinese_days(const char *json, size_t len, int year, hol_year_t *out);
