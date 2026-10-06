#include "holidays.h"

// ---------------------------------------------------------------------------
// 2026 arrangement (State Council of the PRC).
//
// NOTE: verify against the official notice when it is published and update the
// dates below. The structure is deliberately trivial so a yearly edit is a
// two-minute job.
// ---------------------------------------------------------------------------

const holiday_date_t kHolidays2026[] = {
    // New Year
    {2026, 1,  1}, {2026, 1,  2}, {2026, 1,  3},

    // Spring Festival
    {2026, 2, 15}, {2026, 2, 16}, {2026, 2, 17}, {2026, 2, 18},
    {2026, 2, 19}, {2026, 2, 20}, {2026, 2, 21}, {2026, 2, 22},
    {2026, 2, 23},

    // Qingming
    {2026, 4,  4}, {2026, 4,  5}, {2026, 4,  6},

    // Labour Day
    {2026, 5,  1}, {2026, 5,  2}, {2026, 5,  3}, {2026, 5,  4}, {2026, 5, 5},

    // Dragon Boat
    {2026, 6, 19}, {2026, 6, 20}, {2026, 6, 21},

    // Mid-Autumn + National Day (combined into one block in 2026)
    {2026, 9, 25}, {2026, 9, 26},
    {2026, 10, 1}, {2026, 10, 2}, {2026, 10, 3}, {2026, 10, 4},
    {2026, 10, 5}, {2026, 10, 6}, {2026, 10, 7},
};
const int kHolidays2026Count = sizeof(kHolidays2026) / sizeof(kHolidays2026[0]);

const holiday_date_t kMakeupWorkdays2026[] = {
    {2026, 2, 14},   // Saturday before Spring Festival
    {2026, 2, 28},   // Saturday after Spring Festival
    {2026, 5,  9},   // Saturday after Labour Day
    {2026, 9, 27},   // Sunday between Mid-Autumn and National Day
    {2026, 10, 10},  // Saturday after National Day
};
const int kMakeupWorkdays2026Count =
    sizeof(kMakeupWorkdays2026) / sizeof(kMakeupWorkdays2026[0]);

static bool matches(const holiday_date_t *tab, int count, int y, int m, int d)
{
    for (int i = 0; i < count; i++) {
        if (tab[i].year == y && tab[i].month == m && tab[i].day == d) {
            return true;
        }
    }
    return false;
}

bool holidays_year_supported(int year)
{
    return year == 2026;
}

bool holidays_is_holiday(int year, int month, int day)
{
    if (!holidays_year_supported(year)) return false;
    return matches(kHolidays2026, kHolidays2026Count, year, month, day);
}

bool holidays_is_makeup_workday(int year, int month, int day)
{
    if (!holidays_year_supported(year)) return false;
    return matches(kMakeupWorkdays2026, kMakeupWorkdays2026Count, year, month, day);
}
