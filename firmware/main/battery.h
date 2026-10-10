#pragma once

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Battery sense on GPIO4.
//
// The 18650 holder feeds a resistor divider into the ADC, so the pin reads a
// fraction of the cell voltage. Calibration uses the ESP-IDF curve-fitting scheme
// rather than a bare linear factor.
//
// The raw values are carried in the reading so calibration can compare what the
// ADC actually saw against a multimeter on the cell terminals. See
// BATTERY_REPORT_RAW in main.cpp for the measurement mode that prints them.
// ---------------------------------------------------------------------------

typedef struct {
    bool  valid;
    float volts;     // cell voltage after the divider is undone
    int   percent;   // 0..100
    int   raw;       // averaged ADC counts, before any conversion
    int   pin_mv;    // pin voltage after calibration, before the divider
} battery_reading_t;

void battery_init();
battery_reading_t battery_read();

// Voltage -> state of charge, using a 18650 discharge curve and the real empty point
// of about 3.0 V rather than the 2.5 V the vendor example uses.
//
// 2.5 V is below what this board can run at: the ESP32-S3 needs a regulated 3.3 V
// rail and the regulator needs headroom. A map ending at 2.5 V therefore reads about
// 29% at the moment the device actually dies. The straight line is wrong through the
// middle as well, because a Li-ion cell is flat from roughly 3.7 V to 3.9 V.
//
// Separate from battery_read() so the mapping can be exercised against recorded
// voltages without an ADC.
int battery_percent_from_volts(float volts);
