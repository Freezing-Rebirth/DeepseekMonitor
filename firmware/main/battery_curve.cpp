// Battery arithmetic: the discharge curve and the charge-gauge state machine.
//
// Moved out of battery.cpp, which needs the ADC, NVS and the ESP timer; none of that is
// needed to decide what a voltage means, and none of it can be tested on a workstation.

#include "battery_curve.h"

#include <stddef.h>

// ---------------------------------------------------------------------------
// The discharge curve
//
// A straight line between 2.5 V and 4.2 V reads badly in the middle: a Li-ion cell sits
// on a plateau around 3.7-3.9 V for most of its capacity, so the line moves far too
// slowly there. At 3.549 V the old linear map read 62% where the curve reads 19%, an
// error of 43 points.
//
// Resting-voltage profile for a 18650 at 0.2 C, 25 C. Interpolated between points,
// which is what the flat middle needs to stay monotonic without a large table.
// ---------------------------------------------------------------------------
typedef struct {
    uint16_t mv;
    uint8_t  pct;
} soc_point_t;

static const soc_point_t kSocCurve[] = {
    { 3000,   0 },
    { 3200,   4 },
    { 3350,   8 },
    { 3500,  15 },
    { 3620,  25 },
    { 3700,  35 },
    { 3750,  45 },
    { 3800,  55 },
    { 3850,  65 },
    { 3920,  75 },
    { 4000,  85 },
    { 4100,  95 },
    { 4200, 100 },
};

#define SOC_POINTS (sizeof(kSocCurve) / sizeof(kSocCurve[0]))

int battery_curve_percent_from_volts(float volts)
{
    const int mv = (int)(volts * 1000.0f + 0.5f);

    if (mv <= kSocCurve[0].mv) return kSocCurve[0].pct;
    if (mv >= kSocCurve[SOC_POINTS - 1].mv) return kSocCurve[SOC_POINTS - 1].pct;

    for (size_t i = 1; i < SOC_POINTS; i++) {
        const soc_point_t *lo = &kSocCurve[i - 1];
        const soc_point_t *hi = &kSocCurve[i];
        if (mv <= hi->mv) {
            const int span = hi->mv - lo->mv;
            const int num  = (mv - lo->mv) * (hi->pct - lo->pct);
            return lo->pct + (num + span / 2) / span;
        }
    }
    return kSocCurve[SOC_POINTS - 1].pct;
}

// ---------------------------------------------------------------------------
// The gauge
// ---------------------------------------------------------------------------
void battery_gauge_reset(battery_gauge_t *g)
{
    *g = (battery_gauge_t){};
}

// What the status bar shows, derived from the gauge's state.
static void update_state(battery_gauge_t *g)
{
    if (g->charging) {
        // A charger wins over "low": the useful fact is that it is charging, not that the
        // held number is small.
        g->state = g->full ? BAT_STATE_FULL : BAT_STATE_CHARGING;
    } else if (g->published_pct <= BATTERY_LOW_PCT) {
        g->state = BAT_STATE_LOW;
    } else {
        g->state = BAT_STATE_NORMAL;
    }
}

int battery_gauge_apply(battery_gauge_t *g, int raw_pct, int64_t now_us,
                        bool stored_valid, int stored_pct,
                        battery_gauge_event_t *event)
{
    if (event) *event = BATTERY_GAUGE_NOTHING;

    if (!g->have_baseline) {
        g->have_baseline = true;
        g->last_raw_pct  = raw_pct;

        // If this session starts while the charger is already connected, the stored value
        // is the last number the user actually saw at rest; hold it rather than
        // publishing the lifted terminal voltage.
        if (stored_valid && (raw_pct - stored_pct) >= BATTERY_GAUGE_STEP_UP_PCT) {
            g->published_pct     = stored_pct;
            g->charging          = true;
            g->charging_since_us = now_us;
            g->last_rise_us      = now_us;
            if (event) *event = BATTERY_GAUGE_BOOT_CHARGING;
        } else {
            g->published_pct = raw_pct;
        }
        update_state(g);
        return g->published_pct;
    }

    const int delta = raw_pct - g->last_raw_pct;

    if (!g->charging) {
        // A charger makes the terminal voltage lie. Plugging one in lifts the terminal
        // from the open-circuit voltage to the charging voltage in one step - measured on
        // this board, 37% to 67% inside a single repaint interval - and it stays lifted
        // while current flows, which is hours.
        if (delta >= BATTERY_GAUGE_STEP_UP_PCT) {
            g->charging          = true;
            g->charging_since_us = now_us;
            g->last_rise_us      = now_us;
            if (event) *event = BATTERY_GAUGE_CHARGER_ON;
        } else {
            g->published_pct = raw_pct;
        }
    } else {
        const bool sagged =
            (delta <= -BATTERY_GAUGE_SAG_DOWN_PCT);
        const bool gave_up =
            (now_us - g->charging_since_us) > (int64_t)BATTERY_GAUGE_MAX_CHARGING_SEC * 1000000;
        if (sagged || gave_up) {
            // The charger has been removed and the terminal is back on its open-circuit
            // voltage, which is what the curve is calibrated for. Publish it: a cell that
            // really was charged to full shows the new value the moment the cable comes
            // out, and nothing while it is in.
            if (event) *event = sagged ? BATTERY_GAUGE_CHARGER_OFF : BATTERY_GAUGE_GAVE_UP;
            g->charging      = false;
            g->full          = false;
            g->published_pct = raw_pct;
        } else {
            // Still lifted: note when the terminal last climbed, which is what separates
            // the constant-current phase from the constant-voltage plateau at the top.
            if (raw_pct > g->last_raw_pct) g->last_rise_us = now_us;
            if (!g->full && raw_pct >= BATTERY_FULL_PCT &&
                (now_us - g->last_rise_us) >= (int64_t)BATTERY_FULL_PLATEAU_SEC * 1000000) {
                g->full = true;
                if (event) *event = BATTERY_GAUGE_FULL;
            }
        }
    }

    g->last_raw_pct = raw_pct;
    update_state(g);
    return g->published_pct;
}
