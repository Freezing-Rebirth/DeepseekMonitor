// Assemble the panel's view model.
//
// The composition lives here rather than in ui.cpp so the UI stays a pure renderer, and
// rather than in main.cpp so the shape of the panel does not leak into the boot code.
// One function, one job: read each subsystem once and fill the structure.

#include "panel_model.h"

#include <cstdio>
#include <cstring>

#include "app_config.h"
#include "balance_monitor.h"
#include "battery.h"
#include "clock_time.h"
#include "debug_config.h"
#include "pricing.h"
#include "wifi_prov.h"

void panel_model_build(ui_model_t *m)
{
    memset(m, 0, sizeof(*m));

    m->clock_valid = clock_is_synced();
    m->now = clock_now();

    // Precomputed here, not in the paint: wifi_link_text() reads the credential store
    // when the link state is unknown, and a flash read has no business on the LVGL
    // thread.
    snprintf(m->wifi_text, sizeof(m->wifi_text), "%s", wifi_link_text());

    const balance_snapshot_t b = balance_snapshot();
    m->balance_valid   = b.valid;
    m->balance_cny     = b.cny;
    m->ledger_valid    = b.ledger_valid;
    m->daily_burn_cny  = b.daily_burn_cny;
    m->runway_days     = b.runway_days;

    m->model = APP_DEFAULT_MODEL;
    m->tier = APP_DEFAULT_TIER;

    // The charge the device last knew. battery_read() keeps the last good value, which
    // is what makes the boot paint show a number: that paint happens before the main
    // loop's first read, and it used to leave the field at the memset above.
    const battery_reading_t bat = battery_last();
    m->battery_valid   = bat.valid;
    m->battery_percent = bat.percent;
    m->battery_state   = bat.state;

#if DEMO_DATA
    // Fixed sample data, for working on the layout without a network or a charged cell.
    // The token line is derived from the balance and the peak cache-hit rate, so
    // 148.52 CNY at 0.12 CNY per 1M tokens reads as 1.24B.
    m->clock_valid = true;
    m->now = 1779156000;            // 2026-05-19 10:00 Beijing (Tue), PEAK window
    m->battery_valid = true;
    m->battery_percent = 94;
    m->balance_valid = true;
    m->balance_cny = 148.52f;
    m->ledger_valid = true;
    m->daily_burn_cny = 3.86f;
    m->runway_days = 38.0f;
#endif
}
