#pragma once

// Battery arithmetic: the discharge curve, and the state machine that decides what the
// status bar is allowed to publish while a charger is on the terminal.
//
// Pure: no ESP-IDF, no NVS, no clock. Tested on the host in firmware/test.

#include <stdbool.h>
#include <stdint.h>

#include "battery.h"   // battery_state_t

// ---------------------------------------------------------------------------
// Gauge tunables
// ---------------------------------------------------------------------------

// One repaint interval; a real cell cannot fill this much in three minutes.
#define BATTERY_GAUGE_STEP_UP_PCT      6

// Relaxation when the charger is removed, above ADC noise.
#define BATTERY_GAUGE_SAG_DOWN_PCT     2

// Bail-out: if nothing ever looks like the charger coming off (a cell fitted with the
// board already powered, say), the reading wins.
#define BATTERY_GAUGE_MAX_CHARGING_SEC (8 * 3600)

// "Full" cannot be the terminal reaching 4.2 V: that is where the constant-current phase
// ends, with the cell still around 80%. What termination looks like from here is the
// voltage sitting still at the top, so require the high level *and* no rise for ten
// minutes. The cell is about 85% at that point; the rest is the CV taper, which the
// terminal voltage cannot show.
#define BATTERY_FULL_PCT          98
#define BATTERY_FULL_PLATEAU_SEC  (10 * 60)

// Low battery is the other end: 5% is about 3.2 V, roughly the last half hour before
// the 3.0 V floor.
#define BATTERY_LOW_PCT           5

// ---------------------------------------------------------------------------

typedef struct {
    bool    have_baseline;
    bool    charging;
    bool    full;               // latched while charging, so a top-up dip does not flicker
    int     published_pct;
    int     last_raw_pct;
    int64_t charging_since_us;
    int64_t last_rise_us;
    battery_state_t state;
} battery_gauge_t;

// What happened on a call, so the caller can log it. The gauge itself cannot: logging is
// an ESP-IDF facility and this file has no business knowing about one.
typedef enum {
    BATTERY_GAUGE_NOTHING = 0,
    BATTERY_GAUGE_CHARGER_ON,     // a charger appeared; the published value froze
    BATTERY_GAUGE_CHARGER_OFF,    // the charger went; the raw value is published again
    BATTERY_GAUGE_FULL,           // the top plateau was observed
    BATTERY_GAUGE_GAVE_UP,        // held the charging state longer than the limit
    BATTERY_GAUGE_BOOT_CHARGING,  // the session started with a charger on the terminal
} battery_gauge_event_t;

// Percent of charge for a resting cell voltage, interpolated between the curve's points.
// Clamps at both ends.
int battery_curve_percent_from_volts(float volts);

// Forget all state. The next apply() starts a fresh session.
void battery_gauge_reset(battery_gauge_t *g);

// Feed one terminal reading and get back the percentage the panel may publish.
//
// `stored_valid` / `stored_pct` are what the last session left in NVS; they are used once,
// on the first call, to recognise a session that starts with a charger already connected.
// The caller owns that storage - this function does no I/O.
int battery_gauge_apply(battery_gauge_t *g, int raw_pct, int64_t now_us,
                        bool stored_valid, int stored_pct,
                        battery_gauge_event_t *event);
