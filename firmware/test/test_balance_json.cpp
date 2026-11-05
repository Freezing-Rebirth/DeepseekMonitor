// The balance response parser, against payloads shaped like the real one.
//
// Every branch here has a failure mode that the panel would show as a wrong number rather
// than as an error - a missing array reads as an exception, a currency misread shows the
// wrong symbol, a string-vs-number change reads as zero - so each one gets a case.

#include "check.h"
#include "balance_json.h"

#include <string.h>

static ds_balance_t parse(const char *json)
{
    ds_balance_t out = {};
    balance_json_parse(json, strlen(json), &out);
    return out;
}

static bool parse_ok(const char *json, ds_balance_t *out)
{
    *out = (ds_balance_t){};
    return balance_json_parse(json, strlen(json), out);
}

static void run()
{
    // ---- the documented shape: values as strings ----
    const char *cny =
        "{\"is_available\":true,\"balance_infos\":[{"
        "\"currency\":\"CNY\",\"total_balance\":\"148.52\","
        "\"granted_balance\":\"10.00\",\"topped_up_balance\":\"138.52\"}]}";
    ds_balance_t b = {};
    CHECK(parse_ok(cny, &b));
    CHECK_EQ(strcmp(b.currency, "CNY"), 0);
    CHECK(b.total_balance > 148.51f && b.total_balance < 148.53f);
    CHECK(b.granted_balance > 9.99f && b.granted_balance < 10.01f);
    CHECK(b.topped_up_balance > 138.51f && b.topped_up_balance < 138.53f);
    CHECK(b.is_available);

    // ---- numbers instead of strings ----
    const char *numeric =
        "{\"is_available\":false,\"balance_infos\":[{"
        "\"currency\":\"CNY\",\"total_balance\":53.75}]}";
    ds_balance_t n = {};
    CHECK(parse_ok(numeric, &n));
    CHECK(n.total_balance > 53.74f && n.total_balance < 53.76f);
    CHECK(!n.is_available);

    // ---- CNY is preferred when several currencies are present ----
    const char *mixed =
        "{\"balance_infos\":["
        "{\"currency\":\"USD\",\"total_balance\":\"5.00\"},"
        "{\"currency\":\"CNY\",\"total_balance\":\"36.00\"}]}";
    ds_balance_t m = {};
    CHECK(parse_ok(mixed, &m));
    CHECK_EQ(strcmp(m.currency, "CNY"), 0);
    CHECK(m.total_balance > 35.99f && m.total_balance < 36.01f);

    // ---- no CNY: the first entry is used, and its currency is kept ----
    const char *usd_only =
        "{\"balance_infos\":[{\"currency\":\"USD\",\"total_balance\":\"5.00\"}]}";
    ds_balance_t u = {};
    CHECK(parse_ok(usd_only, &u));
    CHECK_EQ(strcmp(u.currency, "USD"), 0);
    CHECK(u.total_balance > 4.99f && u.total_balance < 5.01f);

    // ---- malformed ----
    ds_balance_t bad = {};
    CHECK(!parse_ok("{not json", &bad));
    CHECK_EQ(strcmp(bad.error, "JSON parse failed"), 0);

    // ---- the envelope without the array ----
    ds_balance_t empty = {};
    CHECK(!parse_ok("{\"is_available\":true}", &empty));
    CHECK_EQ(strcmp(empty.error, "balance_infos missing"), 0);

    // ---- an empty array is the same failure ----
    CHECK(!parse_ok("{\"balance_infos\":[]}", &empty));
    CHECK_EQ(strcmp(empty.error, "balance_infos missing"), 0);

    // ---- absent fields stay zero rather than becoming garbage ----
    ds_balance_t bare = {};
    CHECK(parse_ok("{\"balance_infos\":[{\"currency\":\"CNY\"}]}", &bare));
    CHECK_EQ(bare.total_balance, 0.0f);
    CHECK_EQ(bare.currency[0] == 'C' ? 1 : 0, 1);
}

TEST_MAIN(run, "balance_json")
