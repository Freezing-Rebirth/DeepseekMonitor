# 1. One owner for the NVS data namespaces

Date: 2026-11-05
Status: accepted

## Context

Four modules persisted state and each opened NVS itself, with its own key strings:
`battery.cpp` (dsbat), `usage_ledger.cpp` (dsledger), `holidays_net.cpp` and `wifi_prov.cpp`,
the last two both in `dscfg`.

The shared namespace was not noticed until an architecture review listed it: two modules
writing unrelated keys into one namespace, with nothing in either file indicating the other
existed. In the meantime `app_config_store.cpp` carried a comment claiming runtime
configuration lived in one place, which was true of the API key and of nothing else.

## Decision

`store.cpp` owns the data namespaces (`dsbat`, `dsledger`, `dshol`) and every key string,
behind six typed accessors. It is the only file besides `wifi_prov.cpp` that includes
`nvs.h`.

The WiFi credentials stay in `wifi_prov.cpp`. That file already owns them, the provisioning
portal depends on its write path, and once the holiday cache moved out its namespace has no
other tenant. Moving working provisioning code would have bought nothing.

The holiday cache moved to `dshol`. A device upgrading in place loses it once and refetches
on the next hourly task; that is cheaper than keeping a shared namespace to avoid it.

## Consequences

- A new persisted field means a new accessor in `store.cpp`, and nowhere else.
- The battery and ledger namespaces and keys are unchanged, so an in-place upgrade keeps
  its history.
- `store.cpp` is the one part of the firmware that is deliberately not host-tested: it is
  the storage seam. Everything deciding what the numbers mean lives in the pure modules,
  which are.
