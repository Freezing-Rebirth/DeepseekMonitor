#pragma once

// ---------------------------------------------------------------------------
// Compile-time debug switches, shared so every unit agrees.
//
// These live in a header rather than in main.cpp because a #define is per
// translation unit: usage_ledger.cpp cannot see a macro defined in main.cpp, so
// the two would otherwise disagree about whether the diagnostics are on.
//
// All of these are 0 in a release build. The development aids are summarised in
// README.md; the point of this file is that there is exactly one place to look.
// ---------------------------------------------------------------------------

// Periodic housekeeping on every cycle: link state and countdowns each heartbeat,
// the poll cadence, the battery voltage, a line per repaint, and the ledger's
// running totals. Useful while bringing a board up, pure noise once it works,
// because it writes to the console every 30-60 seconds forever.
//
// Event-driven lines are deliberately NOT gated by this. Connection results,
// disconnects with their reason, balance fetches, tariff-state changes and every
// error still print, so a release build stays diagnosable.
#define DEBUG_LOGS 0

// Set to 1 to ignore the cached holiday verdict on the next daily check, so a change
// to the fetch path can be observed without waiting for the cache to expire.
#define HOLIDAYS_FORCE_REFRESH 0
