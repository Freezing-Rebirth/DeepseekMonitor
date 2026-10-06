#pragma once

// ---------------------------------------------------------------------------
// SoftAP provisioning portal.
//
// Raises its own access point and serves a one-page form for the WiFi SSID,
// password and DeepSeek API key. Entering secrets on a phone keeps them out of
// the firmware image and out of any shell history.
//
// Trigger: hold the BOOT (GPIO0) button for ~3 s, or boot with no stored
// credentials.
// ---------------------------------------------------------------------------

// Start the access point and the HTTP form. Returns after the web server is up;
// the caller keeps the portal alive by calling wifi_prov_portal_poll().
bool wifi_prov_portal_start();

// True once the user has submitted credentials.
bool wifi_prov_portal_submitted();

// Stop the server and the access point.
void wifi_prov_portal_stop();

// Poll the BOOT button; returns true on a long press. Safe to call from the
// main loop.
bool boot_button_long_pressed();
