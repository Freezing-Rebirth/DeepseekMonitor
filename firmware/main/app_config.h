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
// the samples already stored. Halving the interval to 5 minutes therefore both
// refreshes the balance sooner and doubles the sample density: the ledger holds 64
// samples, so 5 minutes covers about 5.3 hours of history instead of 10.7.
#define APP_BALANCE_POLL_MS   (5 * 60 * 1000)    // hit the API every 5 minutes
#define APP_UI_REFRESH_MS     (60 * 1000)        // repaint once a minute
#define APP_PRICE_POLL_MS     (10 * 1000)        // re-evaluate peak/trough often
