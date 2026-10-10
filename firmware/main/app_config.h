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

// How long the LVGL task waits when it has nothing to do, and the ceiling on how
// long it will wait even when a timer asks for more.
//
// The panel is reflective and the UI is static between refreshes, so there is
// genuinely nothing to redraw most of the time. lv_timer_handler() reports the delay
// until its next timer and returns 0 when idle; the old floor of 10 ms turned that
// into 100 wakeups a second, roughly 8.6 million a day, to keep concluding that
// nothing had changed. This is the main standing load the firmware controls, and it
// is the first thing to widen if runtime between charges matters more than the
// latency of a single label change.
//
// 250 ms costs at most a quarter second of extra latency on a value change - on a
// display that updates once a minute, that is invisible.
#define APP_LVGL_IDLE_MS      250
#define APP_LVGL_MAX_MS       500
