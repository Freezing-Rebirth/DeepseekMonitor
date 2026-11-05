// The ledger's integer accounting, on the host.
//
// The numbers on the panel's BURN and RUNWAY lines come from here, and they have to
// survive months of accumulation at 1e-4 CNY resolution without drifting - which is why
// the totals are integers, and why this is worth pinning down.

#include "check.h"
#include "ledger_math.h"

static void run()
{
    // ---- CNY <-> hundredths of a fen ----
    CHECK_EQ(ledger_cny_to_cf(0.0f), 0u);
    CHECK_EQ(ledger_cny_to_cf(1.0f), 10000u);
    CHECK_EQ(ledger_cny_to_cf(0.1234f), 1234u);
    CHECK_EQ(ledger_cny_to_cf(53.75f), 537500u);
    CHECK_EQ(ledger_cny_to_cf(-1.0f), 0u);          // saturated, not wrapped
    CHECK(ledger_cf_to_cny(10000u) > 0.9999f && ledger_cf_to_cny(10000u) < 1.0001f);

    // ---- the first reading only sets the reference ----
    ledger_totals_t t = {};
    bool first = false;
    int64_t dt = 0;
    ledger_math_record(&t, ledger_cny_to_cf(10.0f), 1000000, &first, &dt);
    CHECK(first);
    CHECK_EQ(t.count, 1u);
    CHECK_EQ(t.spent_cf, 0u);
    CHECK_EQ(t.elapsed_sec, 0u);
    CHECK_EQ(t.ref_cf, 100000u);

    // ---- a fall in the balance is spend, and the interval is credited ----
    ledger_math_record(&t, ledger_cny_to_cf(9.9f), 1000180, &first, &dt);
    CHECK(!first);
    CHECK_EQ(dt, 180);
    CHECK_EQ(t.count, 2u);
    CHECK_EQ(t.spent_cf, 1000u);        // 0.1 CNY
    CHECK_EQ(t.elapsed_sec, 180u);

    // ---- a rise is a top-up, not negative spend ----
    ledger_math_record(&t, ledger_cny_to_cf(20.0f), 1000360, &first, &dt);
    CHECK_EQ(t.spent_cf, 1000u);
    CHECK_EQ(t.ref_cf, 200000u);

    // ---- an outage longer than the cap is trimmed, not credited in full ----
    //
    // The expected value is the literal, not LEDGER_MAX_INTERVAL_SEC: asserting against
    // the constant under test would let a changed cap pass, which is exactly what a
    // mutation check is for.
    ledger_totals_t u = {};
    ledger_math_record(&u, 100000u, 1000000, &first, &dt);
    ledger_math_record(&u, 100000u, 1000000 + 3 * 24 * 3600, &first, &dt);
    CHECK_EQ(dt, 86400);
    CHECK_EQ(u.elapsed_sec, 86400u);

    // ---- an unusable timestamp changes nothing ----
    ledger_totals_t v = {};
    ledger_math_record(&v, 100000u, 0, &first, &dt);
    CHECK(!first);
    CHECK_EQ(v.count, 0u);

    // ---- stats: not enough history ----
    ledger_totals_t w = {};
    ledger_math_record(&w, ledger_cny_to_cf(10.0f), 1000000, &first, &dt);
    CHECK(!ledger_math_stats(&w, 10.0f).valid);              // one reading

    ledger_math_record(&w, ledger_cny_to_cf(9.99f), 1000000 + 1800, &first, &dt);
    CHECK(!ledger_math_stats(&w, 9.99f).valid);              // 30 min < 1 h

    // ---- stats: one hour of monitored time and some spend ----
    ledger_totals_t s = {};
    ledger_math_record(&s, ledger_cny_to_cf(10.0f), 1000000, &first, &dt);
    // 0.1 CNY spent over exactly one hour: 2.4 CNY/day, 10 CNY left -> 4.17 days.
    ledger_math_record(&s, ledger_cny_to_cf(9.9f), 1000000 + 3600, &first, &dt);
    const ledger_stats_t st = ledger_math_stats(&s, 9.9f);
    CHECK(st.valid);
    CHECK(st.daily_burn_cny > 2.39f && st.daily_burn_cny < 2.41f);
    CHECK(st.runway_days > 4.1f && st.runway_days < 4.2f);

    // ---- stats: spend with no measurable window is not a rate ----
    ledger_totals_t z = {};
    ledger_math_record(&z, ledger_cny_to_cf(10.0f), 1000000, &first, &dt);
    ledger_math_record(&z, ledger_cny_to_cf(9.0f), 1000000, &first, &dt);   // dt = 0
    CHECK(!ledger_math_stats(&z, 9.0f).valid);
}

TEST_MAIN(run, "ledger_math")
