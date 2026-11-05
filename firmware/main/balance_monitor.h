#pragma once

// The balance side of the device: one HTTPS query, and the derived burn/runway
// figures the panel shows.
//
// balance_poll() owns everything about a query - the API key, the TLS round trip, the
// ledger write and the fold back into the burn rate. Callers only ever need the last
// known values, which is what balance_snapshot() returns; nothing outside this module
// keeps a copy of the balance.
typedef struct {
    bool  valid;               // at least one query has succeeded this session
    float cny;                 // balance in CNY, as the API reported it
    bool  ledger_valid;        // the ledger has enough samples to derive a rate
    float daily_burn_cny;
    float runway_days;
} balance_snapshot_t;

// Query the API once and record the result in the ledger.
//
// Blocking: the TLS handshake runs on the caller's task, which is why the provisioning
// task is given 12 KB of stack. Must not be called from the LVGL task.
void balance_poll();

// The last known values. Cheap, no locking: written only by balance_poll(), which runs
// on the main task.
balance_snapshot_t balance_snapshot();
