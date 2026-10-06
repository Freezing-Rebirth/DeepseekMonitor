#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// WiFi bring-up with a softAP provisioning portal.
//
// Credentials live in NVS. On first boot (or when the stored network cannot be
// joined) the device raises its own access point and serves a small form for
// the SSID, password and DeepSeek API key.
// ---------------------------------------------------------------------------

typedef struct {
    bool  connected;
    char  ssid[40];    // matches the internal buffer, so no truncation warning
    char  ip[24];
    int8_t rssi;
} wifi_status_t;

// Link state for the panel's status bar.
//
// WIFI_LINK_NONE means no credentials are stored at all, so there is nothing to
// connect to and "TRY" would be a lie. TRY is shown only once an attempt is
// actually in flight, OK when the station holds an address, and ERR once one has
// failed or a live link has dropped.
typedef enum {
    WIFI_LINK_NONE = 0,
    WIFI_LINK_TRY,
    WIFI_LINK_OK,
    WIFI_LINK_ERR,
} wifi_link_t;

// Initialise NVS + netif + WiFi. Does not block on association.
void wifi_start();

// Block until either the station connects or the timeout expires.
bool wifi_wait_connected(int timeout_ms);

// Push the credentials in NVS into the running driver.
//
// esp_wifi_set_config() copies into the driver's RAM, not into NVS, so after the
// setup form stores new credentials the driver is still holding the old station
// config. Call this between storing and reconnecting, or the reconnect targets the
// wrong network and fails until the next reboot.
bool wifi_apply_stored_credentials();

// Snapshot of the current link state.
wifi_status_t wifi_get_status();

// Current link state, for the status bar.
wifi_link_t wifi_get_link();

// Two-character label for the status bar: "--" with no credentials stored, then
// "TR", "OK" or "ER".
const char *wifi_link_text();

// Credential store (NVS namespace "dscfg").
bool appcfg_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
void appcfg_set_wifi(const char *ssid, const char *pass);
bool appcfg_get_api_key(char *out, size_t out_len);
void appcfg_set_api_key(const char *key);
bool appcfg_has_wifi();
