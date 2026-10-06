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

// Initialise NVS + netif + WiFi. Does not block on association.
void wifi_start();

// Block until either the station connects or the timeout expires.
bool wifi_wait_connected(int timeout_ms);

// Snapshot of the current link state.
wifi_status_t wifi_get_status();

// Credential store (NVS namespace "dscfg").
bool appcfg_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
void appcfg_set_wifi(const char *ssid, const char *pass);
bool appcfg_get_api_key(char *out, size_t out_len);
void appcfg_set_api_key(const char *key);
bool appcfg_has_wifi();
