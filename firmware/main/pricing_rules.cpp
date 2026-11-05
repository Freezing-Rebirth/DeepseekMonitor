// Tariff arithmetic. See pricing_rules.h: pure, and tested on the host.

#include "pricing_rules.h"

// Portability shim, not a dependency: the firmware builds for ESP-IDF, where gmtime_r is
// POSIX and always present, and the host tests build for Windows, where it is not.
#ifdef _WIN32
static void gmtime_r(const time_t *t, struct tm *out) { gmtime_s(out, t); }
#endif

void pricing_rules_beijing_tm(time_t utc, struct tm *out)
{
    time_t bj = utc + PRICING_BEIJING_OFFSET_SEC;
    gmtime_r(&bj, out);
}

bool pricing_rules_in_peak(int hour, int minute)
{
    const int m = hour * 60 + minute;
    return (m >= 9 * 60 && m < 12 * 60) ||   // 09:00-12:00
           (m >= 14 * 60 && m < 18 * 60);    // 14:00-18:00
}

pricing_window_t pricing_rules_classify(const struct tm *beijing, pricing_calendar_t calendar)
{
    // Statutory holidays: off-peak all day.
    if (calendar.is_holiday) return PRICING_WINDOW_OFFPEAK;

    // Weekends are off-peak unless the government made them working days.
    const bool weekend = (beijing->tm_wday == 0 || beijing->tm_wday == 6);
    if (weekend && !calendar.is_makeup_workday) return PRICING_WINDOW_OFFPEAK;

    return pricing_rules_in_peak(beijing->tm_hour, beijing->tm_min)
               ? PRICING_WINDOW_PEAK
               : PRICING_WINDOW_OFFPEAK;
}

int64_t pricing_rules_seconds_to_next_change(time_t utc, pricing_calendar_fn calendar, void *ctx)
{
    struct tm bj;
    pricing_rules_beijing_tm(utc, &bj);
    const pricing_window_t now = pricing_rules_classify(&bj, calendar(&bj, ctx));

    // Start at the next whole minute so we do not report 0 when we are one second away
    // from a boundary.
    time_t probe = utc - (utc % 60) + 60;

    for (int i = 0; i < 7 * 24 * 60 + 2; i++) {
        struct tm p;
        pricing_rules_beijing_tm(probe, &p);
        if (pricing_rules_classify(&p, calendar(&p, ctx)) != now) {
            return (int64_t)(probe - utc);
        }
        probe += 60;
    }
    return 0;
}
