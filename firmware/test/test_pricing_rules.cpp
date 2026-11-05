// Tariff rules, exercised on the host with a calendar the test controls.
//
// The point of these cases is the arithmetic, not the holiday data: pricing.cpp's job
// (looking the calendar up in NVS) is the part that cannot run here, and the part that
// was tested by staring at the panel until now.

#include "check.h"
#include "pricing_rules.h"

#include <time.h>

// MSVC and mingw-spawned runtimes spell the UTC variant differently; the firmware only
// ever uses gmtime_r, so this shim lives in the test.
#ifdef _WIN32
#define timegm _mkgmtime
#endif

// A civil instant in Beijing time, with tm_wday filled in the way production sees it.
static struct tm bj(int year, int month, int day, int hour, int minute)
{
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_isdst = 0;
    const time_t utc = timegm(&t) - PRICING_BEIJING_OFFSET_SEC;
    struct tm out;
    pricing_rules_beijing_tm(utc, &out);
    return out;
}

static time_t bj_utc(int year, int month, int day, int hour, int minute, int sec)
{
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = sec;
    t.tm_isdst = 0;
    return timegm(&t) - PRICING_BEIJING_OFFSET_SEC;
}

// An ordinary working calendar: no holidays, no make-up days.
static pricing_calendar_t ordinary(const struct tm *, void *)
{
    const pricing_calendar_t c = {false, false};
    return c;
}

// One statutory holiday, on a date the test chooses; make-up days ignored.
struct one_holiday { int year, month, day; };

static pricing_calendar_t with_holiday(const struct tm *b, void *ctx)
{
    const one_holiday *h = static_cast<const one_holiday *>(ctx);
    pricing_calendar_t c = {false, false};
    c.is_holiday = (b->tm_year + 1900 == h->year &&
                    b->tm_mon + 1   == h->month &&
                    b->tm_mday      == h->day);
    return c;
}

static pricing_calendar_t every_day_a_makeup_day(const struct tm *, void *)
{
    const pricing_calendar_t c = {false, true};
    return c;
}

static void run()
{
    // ---- the published peak windows, minute by minute at the edges ----
    CHECK(!pricing_rules_in_peak(8, 59));
    CHECK( pricing_rules_in_peak(9, 0));
    CHECK( pricing_rules_in_peak(11, 59));
    CHECK(!pricing_rules_in_peak(12, 0));
    CHECK(!pricing_rules_in_peak(13, 59));
    CHECK( pricing_rules_in_peak(14, 0));
    CHECK( pricing_rules_in_peak(17, 59));
    CHECK(!pricing_rules_in_peak(18, 0));
    CHECK(!pricing_rules_in_peak(23, 59));

    // ---- an ordinary Wednesday ----
    struct tm t = bj(2026, 10, 7, 10, 0);           // 2026-10-07 is a Wednesday
    CHECK_EQ(t.tm_wday, 3);
    CHECK_EQ(pricing_rules_classify(&t, ordinary(&t, nullptr)), PRICING_WINDOW_PEAK);
    t = bj(2026, 10, 7, 8, 0);
    CHECK_EQ(pricing_rules_classify(&t, ordinary(&t, nullptr)), PRICING_WINDOW_OFFPEAK);
    t = bj(2026, 10, 7, 20, 0);
    CHECK_EQ(pricing_rules_classify(&t, ordinary(&t, nullptr)), PRICING_WINDOW_OFFPEAK);

    // ---- a Saturday is off-peak even at 10:00 ----
    t = bj(2026, 10, 10, 10, 0);                    // a Saturday
    CHECK_EQ(t.tm_wday, 6);
    CHECK_EQ(pricing_rules_classify(&t, ordinary(&t, nullptr)), PRICING_WINDOW_OFFPEAK);

    // ---- ... unless it is a make-up working day ----
    CHECK_EQ(pricing_rules_classify(&t, every_day_a_makeup_day(&t, nullptr)), PRICING_WINDOW_PEAK);

    // ---- a statutory holiday is off-peak even on a Wednesday at 10:00 ----
    one_holiday h = {2026, 10, 7};
    t = bj(2026, 10, 7, 10, 0);
    CHECK_EQ(pricing_rules_classify(&t, with_holiday(&t, &h)), PRICING_WINDOW_OFFPEAK);

    // ---- seconds to the next change ----
    // Wednesday 11:59:30 -> 12:00 boundary, in 30 s.
    CHECK_EQ(pricing_rules_seconds_to_next_change(bj_utc(2026, 10, 7, 11, 59, 30), ordinary, nullptr), 30);

    // Wednesday 10:00 -> 12:00, two hours.
    CHECK_EQ(pricing_rules_seconds_to_next_change(bj_utc(2026, 10, 7, 10, 0, 0), ordinary, nullptr), 2 * 3600);

    // Saturday 10:00 -> Monday 09:00: the case a 24-hour lookahead got wrong (it found
    // no transition and reported 0, which the panel drew as a switch happening now).
    CHECK_EQ(pricing_rules_seconds_to_next_change(bj_utc(2026, 10, 10, 10, 0, 0), ordinary, nullptr),
             47 * 3600);

    // Wednesday 12:00 -> 14:00, two hours.
    CHECK_EQ(pricing_rules_seconds_to_next_change(bj_utc(2026, 10, 7, 12, 0, 0), ordinary, nullptr), 2 * 3600);

    // A holiday that swallows a peak window: the change happens when the holiday ends,
    // not at 09:00. 2026-10-07 (Wed) declared a holiday means the next change from
    // Tuesday 10:00 is the following day's 09:00.
    one_holiday hol = {2026, 10, 7};
    CHECK_EQ(pricing_rules_seconds_to_next_change(bj_utc(2026, 10, 7, 10, 0, 0), with_holiday, &hol),
             23 * 3600);
}

TEST_MAIN(run, "pricing_rules")
