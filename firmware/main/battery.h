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
