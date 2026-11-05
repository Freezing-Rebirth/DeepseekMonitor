// The ledger's arithmetic. See ledger_math.h: pure, and tested on the host.

#include "ledger_math.h"

#include <stddef.h>
#include <string.h>

uint32_t ledger_cny_to_cf(float cny)
{
    if (cny <= 0.0f) return 0u;
    const double cf = (double)cny * 10000.0;   // 1 CNY = 100 fen = 10000 cf
    if (cf >= 4294967295.0) return 4294967295u;
    return (uint32_t)(cf + 0.5);
}

float ledger_cf_to_cny(uint32_t cf)
{
    return (float)((double)cf / 10000.0);
}

void ledger_math_record(ledger_totals_t *t, uint32_t now_cf, int64_t unix_ts,
                        bool *first, int64_t *dt_out)
{
    if (first)   *first = false;
    if (dt_out)  *dt_out = 0;
    if (unix_ts <= 0) return;

    if (t->count == 0) {
        // First ever reading: nothing to compare against yet.
        t->ref_cf  = now_cf;
        t->count   = 1;
        t->last_ts = (uint32_t)unix_ts;
        if (first) *first = true;
        return;
    }

    // Credit the interval since the previous reading, taken from wall-clock timestamps so
    // the time survives a reboot. A span longer than LEDGER_MAX_INTERVAL_SEC means the
    // device was off or offline; only that cap is credited, so unmonitored time cannot
    // dilute the average.
    int64_t dt = 0;
    if (t->last_ts > 0 && unix_ts > (int64_t)t->last_ts) {
        dt = unix_ts - (int64_t)t->last_ts;
    }
    if (dt > LEDGER_MAX_INTERVAL_SEC) dt = LEDGER_MAX_INTERVAL_SEC;
    if (dt > 0) t->elapsed_sec += (uint32_t)dt;
    if (dt_out) *dt_out = dt;

    if (now_cf < t->ref_cf) {
        t->spent_cf += (t->ref_cf - now_cf);
    }
    // A higher balance means a top-up (or a corrected reading): the reference simply moves
    // up and no spend is recorded for the interval.
    t->ref_cf  = now_cf;
    t->count++;
    t->last_ts = (uint32_t)unix_ts;
}

ledger_stats_t ledger_math_stats(const ledger_totals_t *t, float current_balance_cny)
{
    ledger_stats_t out = {};
    if (!t || t->count < 2) return out;

    const double spent  = (double)ledger_cf_to_cny(t->spent_cf);
    const int64_t window = (int64_t)t->elapsed_sec;

    if (window < LEDGER_MIN_WINDOW_SEC || spent <= 0.0) {
        return out;
    }

    out.valid = true;
    // The window is everything since monitoring began, so this is the average rate over
    // the whole monitored period rather than over a short recent slice.
    out.daily_burn_cny = (float)(spent * 86400.0 / (double)window);
    out.runway_days = (out.daily_burn_cny > 0.0001f)
                          ? (current_balance_cny / out.daily_burn_cny)
                          : 0.0f;
    return out;
}
