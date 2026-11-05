// The discharge curve and the charge gauge, on the host.
//
// These two pieces decide what the status bar says and they are the ones that have been
// wrong in ways only a user noticed: a linear map that read 62% where the cell had 19%,
// and a gauge that published a 30-point jump when a charger was plugged in.

#include "check.h"
#include "battery_curve.h"

static const int64_t SEC = 1000000;

static void run()
{
    // ---- the curve: the published points ----
    CHECK_EQ(battery_curve_percent_from_volts(3.00f), 0);
    CHECK_EQ(battery_curve_percent_from_volts(3.20f), 4);
    CHECK_EQ(battery_curve_percent_from_volts(3.50f), 15);
    CHECK_EQ(battery_curve_percent_from_volts(3.70f), 35);
    CHECK_EQ(battery_curve_percent_from_volts(3.80f), 55);
    CHECK_EQ(battery_curve_percent_from_volts(4.20f), 100);

    // ---- clamped outside the curve ----
    CHECK_EQ(battery_curve_percent_from_volts(2.50f), 0);
    CHECK_EQ(battery_curve_percent_from_volts(4.30f), 100);

    // ---- monotonic across the whole range ----
    bool monotonic = true;
    int prev = -1;
    for (int mv = 2900; mv <= 4300; mv += 10) {
        const int p = battery_curve_percent_from_volts((float)mv / 1000.0f);
        if (p < prev) monotonic = false;
        prev = p;
    }
    CHECK(monotonic);

    // ---- the middle is not a straight line ----
    // 3.549 V is the case the vendor map got wrong: 62% on a line between 2.5 and 4.2 V,
    // 19 on this curve.
    CHECK_EQ(battery_curve_percent_from_volts(3.549f), 19);

    // ---- the gauge: a normal discharge ----
    battery_gauge_t g;
    battery_gauge_event_t ev;
    battery_gauge_reset(&g);
    CHECK_EQ(battery_gauge_apply(&g, 40, 0, false, 0, &ev), 40);
    CHECK_EQ(ev, BATTERY_GAUGE_NOTHING);
    CHECK_EQ(g.state, BAT_STATE_NORMAL);
    CHECK_EQ(battery_gauge_apply(&g, 39, 600 * SEC, false, 0, &ev), 39);
    CHECK_EQ(ev, BATTERY_GAUGE_NOTHING);

    // ---- a charger: a 6-point step freezes the published value ----
    CHECK_EQ(battery_gauge_apply(&g, 46, 1200 * SEC, false, 0, &ev), 39);
    CHECK_EQ(ev, BATTERY_GAUGE_CHARGER_ON);
    CHECK_EQ(g.state, BAT_STATE_CHARGING);
    // still climbing, still held
    CHECK_EQ(battery_gauge_apply(&g, 60, 1800 * SEC, false, 0, &ev), 39);
    CHECK_EQ(ev, BATTERY_GAUGE_NOTHING);

    // ---- the top plateau: 98% with no rise for ten minutes is "full" ----
    CHECK_EQ(battery_gauge_apply(&g, 98, 2400 * SEC, false, 0, &ev), 39);   // a rise here
    CHECK_EQ(ev, BATTERY_GAUGE_NOTHING);
    CHECK_EQ(battery_gauge_apply(&g, 98, 2400 * SEC + 9 * 60 * SEC, false, 0, &ev), 39);
    CHECK_EQ(ev, BATTERY_GAUGE_NOTHING);
    CHECK_EQ(battery_gauge_apply(&g, 98, 2400 * SEC + 10 * 60 * SEC, false, 0, &ev), 39);
    CHECK_EQ(ev, BATTERY_GAUGE_FULL);
    CHECK_EQ(g.state, BAT_STATE_FULL);

    // ---- the charger goes: a 2-point sag publishes the resting value ----
    CHECK_EQ(battery_gauge_apply(&g, 96, 3000 * SEC, false, 0, &ev), 96);
    CHECK_EQ(ev, BATTERY_GAUGE_CHARGER_OFF);
    CHECK_EQ(g.state, BAT_STATE_NORMAL);

    // ---- a charger that is already on the terminal at boot ----
    battery_gauge_reset(&g);
    CHECK_EQ(battery_gauge_apply(&g, 67, 0, true, 37, &ev), 37);
    CHECK_EQ(ev, BATTERY_GAUGE_BOOT_CHARGING);
    CHECK_EQ(g.state, BAT_STATE_CHARGING);

    // ... and one that is not: the reading is published as it arrives
    battery_gauge_reset(&g);
    CHECK_EQ(battery_gauge_apply(&g, 40, 0, true, 38, &ev), 40);
    CHECK_EQ(ev, BATTERY_GAUGE_NOTHING);

    // ---- the bail-out: nothing ever looks like the charger coming off ----
    battery_gauge_reset(&g);
    battery_gauge_apply(&g, 50, 0, false, 0, &ev);
    battery_gauge_apply(&g, 56, 1 * SEC, false, 0, &ev);
    CHECK_EQ(battery_gauge_apply(&g, 56, 1 * SEC + (8 * 3600 + 1) * SEC, false, 0, &ev), 56);
    CHECK_EQ(ev, BATTERY_GAUGE_GAVE_UP);

    // ---- low battery, and a charger that overrides it ----
    battery_gauge_reset(&g);
    CHECK_EQ(battery_gauge_apply(&g, 4, 0, false, 0, &ev), 4);
    CHECK_EQ(g.state, BAT_STATE_LOW);
    battery_gauge_apply(&g, 10, 1 * SEC, false, 0, &ev);
    CHECK_EQ(g.state, BAT_STATE_CHARGING);
}

TEST_MAIN(run, "battery_curve")
