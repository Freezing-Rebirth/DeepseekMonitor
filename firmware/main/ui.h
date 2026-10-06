#pragma once

#include <stdbool.h>
#include <time.h>
#include "lvgl.h"
#include "pricing.h"

// ---------------------------------------------------------------------------
// Panel UI.
//
// The layout mirrors the 400x300 design exports position for position. Those
// positions were measured in a real browser rather than estimated; see
// _research/measure_design.js and _research/design_metrics.json.
//
// Bands (2 px light frame around the whole panel):
//   y   2.. 25   status header      chrome background
//   y  26..160   funds telemetry    chrome background
//   y 161..297   tariff panel       inverted against the chrome
//
// The state inversion is the whole point of the two mockups: peak paints dark
// chrome with a light tariff panel, off-peak paints light chrome with a dark
// panel.
// ---------------------------------------------------------------------------
#define UI_WIDTH   400
#define UI_HEIGHT  300

// The design's three bands: a 26 px status bar, a 124 px balance block, and a
// 150 px pricing block whose background flips with the tariff state.
#define UI_STATUS_Y   0
#define UI_BALANCE_Y  26
#define UI_PRICE_Y    150

// Everything the panel needs for one repaint.
typedef struct {
    bool   clock_valid;      // SNTP has produced a real date
    time_t now;              // UTC

    bool   wifi_connected;
    int8_t wifi_rssi;
    bool   battery_valid;
    float  battery_volts;
    int    battery_percent;

    bool   balance_valid;
    float  balance_cny;
    char   balance_currency[8];

    bool   ledger_valid;
    float  daily_burn_cny;
    float  runway_days;

    price_model_t model;
    price_tier_t  tier;

    bool   api_ok;           // last API poll succeeded
    char   status_text[48];  // short diagnostic line
} ui_model_t;

// Build every LVGL object once. The panel driver and LVGL must already be up.
void ui_init(lv_display_t *disp);

// Push new values into the existing objects. `model->now` drives the tariff
// classification, so the peak/off-peak inversion happens here.
void ui_update(const ui_model_t *model);

// Force a repaint.
void ui_invalidate();

// Advance the bottom-left activity indicator. The indicator is the one element
// that is not part of the design; it shows that data is still being refreshed.
void ui_set_heartbeat(bool alive);
