#pragma once

// Offline check of the tariff rules: sweeps a week hour by hour and logs the
// state plus the next switch, then asserts a handful of specific expectations.
// Called once at boot when PRICING_SELFTEST is enabled.
//
// Declaration and definition share C++ linkage: wrapping only the header in
// extern "C" produced a different symbol and failed to link.
void pricing_selftest();
