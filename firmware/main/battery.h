#pragma once

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Battery sense on GPIO4.
//
// The 18650 holder feeds a 3x resistor divider into the ADC, so the pin reads
// roughly one third of the cell voltage. Calibration uses the ESP-IDF curve
// fitting scheme rather than a bare linear factor.
// ---------------------------------------------------------------------------

typedef struct {
    bool  valid;
    float volts;     // cell voltage after the divider is undone
    int   percent;   // 0..100
} battery_reading_t;

void battery_init();
battery_reading_t battery_read();
