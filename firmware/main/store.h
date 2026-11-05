#pragma once

// The one owner of the NVS data namespaces.
//
// Three modules persist state - the battery's last reading, the ledger's totals, the
// holiday cache - and each used to open NVS itself and keep its own key strings. That is
// how the holiday blob ended up sharing "dscfg" with the WiFi credentials: two modules
// writing into one namespace with unrelated keys, and no way to see the collision by
// reading either file.
//
// The WiFi credentials stay where they are. wifi_prov.cpp is already their only owner,
// its namespace has no other tenant, and moving them would mean rewriting the portal's
// write path for no gain.
//
// This is the storage seam; it is not pure and it is not host-tested. Everything that
// decides what the numbers mean lives in the *_rules / *_curve / *_math / *_json modules,
// which are.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ledger_math.h"

// ---- battery: the last reading that succeeded ----
bool store_battery_load(uint8_t *pct, uint32_t *mv);
void store_battery_save(uint8_t pct, uint32_t mv);

// ---- ledger: the running totals ----
bool store_ledger_load(ledger_totals_t *out);
void store_ledger_save(const ledger_totals_t *t);

// ---- holidays: the cached table blob ----
bool store_holidays_load(void *buf, size_t len);
void store_holidays_save(const void *buf, size_t len);
