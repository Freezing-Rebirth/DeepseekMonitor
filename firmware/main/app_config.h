#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Build-time defaults. Everything here can be overridden at runtime through
// the provisioning portal, which stores values in NVS.
// ---------------------------------------------------------------------------

// DeepSeek API
#define APP_DS_BASE_URL        "https://api.deepseek.com"
#define APP_DS_BALANCE_PATH    "/user/balance"

// Leave empty to force the provisioning portal on first boot.
#define APP_DS_DEFAULT_API_KEY ""

// WiFi fallback credentials (empty => always start the portal)
#define APP_WIFI_DEFAULT_SSID  ""
#define APP_WIFI_DEFAULT_PASS  ""

// SoftAP name used by the provisioning portal.
#define APP_PROV_AP_SSID       "DeepSeek-Monitor"

// Which model and which billing component is surfaced on the panel.
// The canonical enumerations live in pricing.h (price_model_t / price_tier_t);
// these macros are consumed there, so the values must match.
#define APP_DEFAULT_MODEL PM_FLASH
#define APP_DEFAULT_TIER  PT_CACHE_HIT

// Polling cadence, milliseconds.
//
// The balance poll also records a ledger sample, and BURN/RUNWAY are derived from
// the running totals. A shorter interval means less consumption is lost when a
// top-up lands in the same window as spending, but each poll costs a TLS handshake
// and a radio wake-up, so the interval is a power/accuracy trade:
//
//   1 minute  ~0.2% of a top-up window's spend lost, ~28 years of NVS life
//   3 minutes ~0.6%,                              ~38 years        <- chosen
//   5 minutes ~1.0%,                              ~143 years
//
// Three minutes is the compromise: the top-up error stays well under a percent
// while the radio wakes a fifth as often as it would at one minute.
#define APP_BALANCE_POLL_MS   (3 * 60 * 1000)    // hit the API every 3 minutes
#define APP_UI_REFRESH_MS     (60 * 1000)        // repaint once a minute
#define APP_PRICE_POLL_MS     (10 * 1000)        // re-evaluate peak/trough often
