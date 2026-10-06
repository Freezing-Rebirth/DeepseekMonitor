#pragma once

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Local spend ledger.
//
// DeepSeek exposes only GET /user/balance - there is no spending-history or
// billing endpoint. BURN RATE and RUNWAY are therefore derived on-device.
//
// The ledger keeps two running totals in NVS rather than a window of samples:
//
//     spent_cf      total CNY spent since monitoring began, in 0.01 fen units
//     elapsed_s     total seconds monitored
//     ref_balance   the balance the next delta is measured against
//
// so BURN is exactly "total spend / total time monitored", and the window grows
// without bound instead of being capped by the number of samples a buffer holds.
// The earlier design kept a 64-entry ring of (timestamp, balance) pairs, which
// only ever described the last ~5 hours; anything older was overwritten.
//
// Counters rather than a blob on purpose: the NVS docs single out blobs as the
// worst case for wear levelling ("one page must hold the whole blob"), while an
// integer entry needs a single free slot. spent_cf is an integer for the same
// reason - a float accumulator would drift over months of additions.
// ---------------------------------------------------------------------------

// Smallest monitored span that yields a trustworthy rate (1 hour).
#define LEDGER_MIN_WINDOW_SEC (3600)

typedef struct {
    bool  valid;          // enough history for a rate
    float daily_burn_cny; // total spend / total monitored time, per day
    float runway_days;    // balance / daily burn, or 0 when unknown
} ledger_stats_t;

void ledger_init();

// Fold a fresh balance reading into the running totals. Call after every
// successful API poll.
void ledger_record(float total_balance_cny, int64_t unix_ts);

// Compute burn rate and runway from the running totals.
ledger_stats_t ledger_stats(float current_balance_cny, int64_t unix_ts);

// Diagnostics: number of readings folded in and the monitored span in seconds.
int     ledger_sample_count();
int64_t ledger_window_sec();
