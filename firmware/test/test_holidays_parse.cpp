// The holiday response parsers, against payloads in each accepted shape.
//
// A parser that returns the wrong list does not fail loudly: it makes the device bill peak
// rates on a statutory holiday, for as long as the cache lives. That is why these get cases
// rather than only a smoke test.

#include "check.h"
#include "holidays_parse.h"

#include <string.h>

static int today(const char *json)
{
    return holidays_parse_today(json, strlen(json));
}

static bool year_ok(bool (*fn)(const char *, size_t, int, hol_year_t *), const char *json, int year, hol_year_t *out)
{
    *out = (hol_year_t){};
    return fn(json, strlen(json), year, out);
}

static void run()
{
    // ---- today: timor.tech ----
    CHECK_EQ(today("{\"code\":0,\"type\":{\"type\":2,\"name\":\"x\"}}"), DAY_HOLIDAY);
    CHECK_EQ(today("{\"code\":0,\"type\":{\"type\":0}}"), DAY_WORKDAY);

    // ---- today: dreace's text form ----
    CHECK_EQ(today("{\"isHoliday\":true,\"type\":\"假日\"}"), DAY_HOLIDAY);
    CHECK_EQ(today("{\"isHoliday\":false,\"type\":\"workday\"}"), DAY_WORKDAY);

    // ---- today: xiaoai.me ----
    CHECK_EQ(today("{\"code\":0,\"data\":[{\"daytype\":3}]}"), DAY_MAKEUP);

    // ---- today: not JSON, or nothing recognisable ----
    CHECK_EQ(today("{oops"), DAY_UNKNOWN);
    CHECK_EQ(today("{\"code\":0}"), DAY_UNKNOWN);

    // ---- year: timor ----
    const char *timor =
        "{\"code\":0,\"holiday\":{"
        "\"01-01\":{\"holiday\":true,\"date\":\"2026-01-01\"},"
        "\"01-04\":{\"holiday\":false,\"date\":\"2026-01-04\"}}}";
    hol_year_t y = {};
    CHECK(year_ok(holidays_parse_year_timor, timor, 2026, &y));
    CHECK_EQ(y.year, 2026);
    CHECK_EQ(y.holidays.count, 1);
    CHECK_EQ(y.holidays.month[0], 1);
    CHECK_EQ(y.holidays.day[0], 1);
    CHECK_EQ(y.makeup.count, 1);
    CHECK_EQ(y.makeup.day[0], 4);

    // the wrong year is rejected rather than trusted
    CHECK(!year_ok(holidays_parse_year_timor, timor, 2027, &y));

    // an empty table would silently mean "every day is a working day"
    CHECK(!year_ok(holidays_parse_year_timor,
                   "{\"code\":0,\"holiday\":{\"01-04\":{\"holiday\":false,\"date\":\"2026-01-04\"}}}",
                   2026, &y));

    // ---- year: xiaoai.me ----
    const char *xiaoai =
        "{\"code\":0,\"data\":["
        "{\"daytype\":1,\"date\":\"2026-10-01\"},"
        "{\"daytype\":3,\"date\":\"2026-10-10\"},"
        "{\"daytype\":0,\"date\":\"2026-10-11\"}]}";
    CHECK(year_ok(holidays_parse_year_xiaoai, xiaoai, 2026, &y));
    CHECK_EQ(y.holidays.count, 1);
    CHECK_EQ(y.holidays.month[0], 10);
    CHECK_EQ(y.holidays.day[0], 1);
    CHECK_EQ(y.makeup.count, 1);
    CHECK_EQ(y.makeup.day[0], 10);

    // ---- year: chinese-days ----
    const char *cd =
        "{\"holidays\":{\"2026-10-01\":\"x\",\"2026-10-02\":\"y\"},"
        "\"workdays\":{\"2026-10-10\":\"z\"}}";
    CHECK(year_ok(holidays_parse_year_chinese_days, cd, 2026, &y));
    CHECK_EQ(y.holidays.count, 2);
    CHECK_EQ(y.holidays.day[1], 2);
    CHECK_EQ(y.makeup.count, 1);
    CHECK_EQ(y.makeup.day[0], 10);

    // days from another year in the same release are ignored
    CHECK(year_ok(holidays_parse_year_chinese_days,
                  "{\"holidays\":{\"2026-01-01\":\"x\",\"2027-01-01\":\"y\"}}", 2026, &y));
    CHECK_EQ(y.holidays.count, 1);

    // no holidays object at all
    CHECK(!year_ok(holidays_parse_year_chinese_days, "{\"workdays\":{}}", 2026, &y));
}

TEST_MAIN(run, "holidays_parse")
