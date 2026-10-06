#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Network time: SNTP with a Beijing-time civil clock.
//
// The PCF85063A RTC on the board has no backup battery, so it cannot survive a
// power cycle. SNTP is therefore the authority; the RTC is not used.
// ---------------------------------------------------------------------------

// Start the SNTP client and block until the clock is set or the timeout
// expires. Returns true when the system time is synchronised.
bool clock_sync_sntp(int timeout_ms);

// True once the system clock has been synchronised at least once.
bool clock_is_synced();

// Format the current Beijing time as "HH:MM CST".
void clock_format_hhmm_cst(char *out, size_t out_len);

// Current UTC time.
time_t clock_now();
