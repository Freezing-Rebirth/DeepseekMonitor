#pragma once

// ---------------------------------------------------------------------------
// SoftAP provisioning portal.
//
// Raises its own access point and serves a one-page form for the WiFi SSID,
// password and DeepSeek API key. Entering secrets on a phone keeps them out of
// the firmware image and out of any shell history.
//
// Raised in three situations:
//   * boot with no stored credentials
//   * boot with credentials that will not connect
//   * a BOOT (GPIO0) long press at any time
// ---------------------------------------------------------------------------

// Start the access point and the HTTP form. Returns after the web server is up;
// the caller keeps the portal alive by polling wifi_prov_portal_submitted().
bool wifi_prov_portal_start();

// True once the user has submitted credentials.
bool wifi_prov_portal_submitted();

// Clear the submitted flag. Call before starting the portal for a second time,
// otherwise the previous submission is still latched and the wait loop falls
// through immediately.
void wifi_prov_portal_clear_submitted();

// Stop the server and the access point.
void wifi_prov_portal_stop();

// While the portal is open the station must not retry. The radio is in APSTA mode,
// so a station looping on reconnects competes with the access point and the setup
// form's requests are dropped. wifi_portal.cpp drives this automatically around
// start/stop; it is exposed for callers that manage the mode themselves.
void wifi_prov_suspend_station(bool suspend);

// Poll the BOOT button; returns true on a long press. Safe to call from the
// main loop.
bool boot_button_long_pressed();
