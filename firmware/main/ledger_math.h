#pragma once

// The ledger's arithmetic: integer-cent accounting, interval capping, and the burn/runway
// derivation.
//
// Pure: no NVS, no clock, no logging. usage_ledger.cpp keeps the storage and the log
// lines; this file decides what the numbers mean, and is tested on the host.

#include <stdbool.h>
#include <stdint.h>

#include "usage_ledger.h"   // ledger_stats_t, LEDGER_MIN_WINDOW_SEC

// Longest single interval that may be credited to the totals.
//
// A reading is taken every three minutes, so anything much longer means the device was
// off or offline. The cap exists so an outage cannot dump unmonitored time into the
// average and drag BURN down - but it must not be so tight that a genuine outage distorts
// things the other way. Spend and time are credited separately (spend from the balance
// delta, time from this interval), so capping the interval at two hours while still
// crediting three days of real spend inflated BURN by about 36x. At 24 hours the two stay
// in proportion for any plausible outage, and only a multi-day gap is trimmed.
#define LEDGER_MAX_INTERVAL_SEC (24 * 3600)

// The persisted totals, exactly as they sit in NVS.
typedef struct {
    uint32_t count;        // readings folded in
    uint32_t spent_cf;     // total spend, hundredths of a fen (1e-4 CNY)
    uint32_t elapsed_sec;  // total monitored seconds
    uint32_t ref_cf;       // balance the next delta is measured against
    uint32_t last_ts;      // unix seconds of the previous reading
} ledger_totals_t;

// CNY to hundredths of a fen. Saturates rather than wrapping; negatives become 0.
uint32_t ledger_cny_to_cf(float cny);
float    ledger_cf_to_cny(uint32_t cf);

// Fold one reading into the totals. `first` reports that this was the first ever reading
// (nothing to compare against yet); `dt_out` is the interval credited, which the caller
// logs.
void ledger_math_record(ledger_totals_t *t, uint32_t now_cf, int64_t unix_ts,
                        bool *first, int64_t *dt_out);

// Burn rate and runway. Invalid until there is both enough monitored time and some spend.
ledger_stats_t ledger_math_stats(const ledger_totals_t *t, float current_balance_cny);
