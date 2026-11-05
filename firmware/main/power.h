#pragma once

// Power policy: dynamic frequency scaling plus automatic light sleep.
//
// The application's only power decision. Called once, before anything that could
// enter a low-power state, and never again - esp_pm_configure() reconfigures the
// running governor rather than layering.
void power_configure();
