#include "wifi_prov.h"
#include "app_config.h"

#include <string.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs.h>

static const char *TAG = "wifi";

#define CFG_NS "dscfg"

static bool s_connected = false;
static char s_ip[24] = "0.0.0.0";
static char s_ssid[40] = {0};
// The RSSI is read straight from esp_wifi_sta_get_ap_info() in wifi_get_status(),
// so it is deliberately not cached here.

// ---------------------------------------------------------------------------
// Link state for the status bar.
//
// "TRY" while an association is in flight, "OK" once an address is held, "ERR"
// once the driver reports a failed attempt or a live link drops, and "--" when no
// credentials are stored at all - in that case nothing is being attempted, so
// showing TRY would claim activity that is not happening.
//
// Any new attempt moves it back to TRY, so the panel shows the current situation
// rather than a stale verdict.
// ---------------------------------------------------------------------------
static volatile wifi_link_t s_link = WIFI_LINK_NONE;

// Set once wifi_start() has brought NVS up. Guards the credential lookups so an
// early status query cannot produce spurious NVS error logs.
static volatile bool s_nvs_ready = false;

// How long a failure stays on screen before the driver's next retry moves it back
// to TRY. Long enough to be read, short enough not to look frozen.
#define WIFI_ERR_HOLD_MS 4000
static volatile TickType_t s_err_at = 0;

wifi_link_t wifi_get_link()
{
    // No credentials means no attempt, whatever the driver is doing.
    if (!appcfg_has_wifi()) {
        s_link = WIFI_LINK_NONE;
        return WIFI_LINK_NONE;
    }
    if (s_link == WIFI_LINK_NONE) {
        // Credentials have just been stored; an attempt is about to start.
        s_link = WIFI_LINK_TRY;
    }
    if (s_link == WIFI_LINK_ERR && s_err_at != 0) {
        const TickType_t elapsed = xTaskGetTickCount() - s_err_at;
        if (elapsed >= pdMS_TO_TICKS(WIFI_ERR_HOLD_MS)) {
            s_link = WIFI_LINK_TRY;
            s_err_at = 0;
        }
    }
    return s_link;
}

const char *wifi_link_text()
{
    switch (wifi_get_link()) {
        case WIFI_LINK_OK:  return "OK";
        case WIFI_LINK_ERR: return "ER";
        case WIFI_LINK_TRY: return "TR";
        default:            return "--";
    }
}

// ---------------------------------------------------------------------------
// Credential store
// ---------------------------------------------------------------------------
static bool nvs_get_str(const char *key, char *out, size_t out_len)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = out_len;
    const esp_err_t err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    return err == ESP_OK;
}

static void nvs_set_str(const char *key, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, val);
    nvs_commit(h);
    nvs_close(h);
}

bool appcfg_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    ssid[0] = '\0';
    pass[0] = '\0';
    const bool ok = nvs_get_str("ssid", ssid, ssid_len);
    if (ok) nvs_get_str("pass", pass, pass_len);
    return ok && ssid[0] != '\0';
}

void appcfg_set_wifi(const char *ssid, const char *pass)
{
    nvs_set_str("ssid", ssid);
    nvs_set_str("pass", pass);
}

bool appcfg_get_api_key(char *out, size_t out_len)
{
    out[0] = '\0';
    if (nvs_get_str("apikey", out, out_len) && out[0] != '\0') return true;
    if (APP_DS_DEFAULT_API_KEY[0] != '\0') {
        snprintf(out, out_len, "%s", APP_DS_DEFAULT_API_KEY);
        return true;
    }
    return false;
}

void appcfg_set_api_key(const char *key) { nvs_set_str("apikey", key); }

bool appcfg_has_wifi()
{
    // nvs_get_str() logs an error and returns false when NVS is not up yet, which
    // happens if the UI asks for the link state before wifi_start(). Report "no
    // credentials" quietly in that window instead of writing an error line.
    if (!s_nvs_ready) return false;
    char s[33], p[64];
    return appcfg_get_wifi(s, sizeof(s), p, sizeof(p));
}

// ---------------------------------------------------------------------------
// WiFi events
// ---------------------------------------------------------------------------
// Set while the provisioning portal is up. The station must not retry then: the
// radio is in APSTA mode, so a station stuck in a reconnect loop competes with the
// access point for the single radio, and the setup form's requests get dropped.
// That is what stopped a submission from ever reaching the device.
// ---------------------------------------------------------------------------
static volatile bool s_suspend_sta = false;

void wifi_prov_suspend_station(bool suspend)
{
    s_suspend_sta = suspend;
    if (suspend) {
        // Stop the retry loop now rather than waiting for the next event.
        esp_wifi_disconnect();
        // With no station attempt in flight there is nothing to report, so park at
        // NONE rather than leaving a stale OK or ERR on the panel. wifi_get_link()
        // turns that into TRY only once credentials actually exist.
        s_link = WIFI_LINK_NONE;
        s_err_at = 0;
        ESP_LOGI(TAG, "station retries suspended while the portal is open");
    } else {
        s_link = WIFI_LINK_NONE;   // recomputed on the next status read
        esp_wifi_connect();
    }
}

static void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_link = WIFI_LINK_TRY;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        // Associated, but no address yet: still "trying" until DHCP completes.
        s_link = WIFI_LINK_TRY;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d =
            (const wifi_event_sta_disconnected_t *)data;
        s_connected = false;
        if (s_suspend_sta) {
            // Expected while the portal is open; do not retry and do not log, or the
            // loop floods the console and drowns the portal's own messages.
            return;
        }
        // Log the real reason before retrying. Without it a join failure is
        // indistinguishable from an authentication failure, an AP that refuses the
        // association, or a DHCP timeout - all of which look like "disconnected".
        ESP_LOGW(TAG, "disconnected (reason %d, bssid " MACSTR "), retrying",
                 d ? d->reason : -1, MAC2STR(d ? d->bssid : (uint8_t[6]){0}));
        s_link = WIFI_LINK_ERR;
        s_err_at = xTaskGetTickCount();
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!s_suspend_sta) {
            s_link = WIFI_LINK_TRY;
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_connected = true;
        s_link = WIFI_LINK_OK;
        s_err_at = 0;
        ESP_LOGI(TAG, "got IP %s", s_ip);
    }
}

// Push the stored credentials into the running driver.
//
// esp_wifi_set_config() copies into the driver's own RAM, not into NVS. Writing new
// credentials from the setup form therefore left the driver holding the previous
// (often empty) station config, so the reconnect that followed the form used the
// wrong network and failed - while a reboot, which re-runs wifi_start() and reloads
// from NVS, connected immediately. This is the piece that fixes that.
bool wifi_apply_stored_credentials()
{
    char ssid[33] = {0};
    char pass[64] = {0};
    if (!appcfg_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGW(TAG, "no credentials to apply");
        return false;
    }

    wifi_config_t wc = {};
    const size_t ssid_max = sizeof(wc.sta.ssid) - 1;       // 31
    const size_t pass_max = sizeof(wc.sta.password) - 1;   // 63
    memcpy(wc.sta.ssid, ssid, strnlen(ssid, ssid_max));
    memcpy(wc.sta.password, pass, strnlen(pass, pass_max));
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;

    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not apply credentials for '%s': %s",
                 ssid, esp_err_to_name(err));
        return false;
    }
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    ESP_LOGI(TAG, "applied credentials for '%s' to the running driver", ssid);
    return true;
}

void wifi_start()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // From here the credential lookups are meaningful.
    s_nvs_ready = true;
    // No credentials means nothing is being attempted yet, so the panel starts at
    // "--" rather than claiming a connection attempt.
    s_link = appcfg_has_wifi() ? WIFI_LINK_TRY : WIFI_LINK_NONE;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    char ssid[33] = {0};
    char pass[64] = {0};
    if (!appcfg_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass))) {
        if (APP_WIFI_DEFAULT_SSID[0] != '\0') {
            snprintf(ssid, sizeof(ssid), "%s", APP_WIFI_DEFAULT_SSID);
            snprintf(pass, sizeof(pass), "%s", APP_WIFI_DEFAULT_PASS);
        }
    }

    wifi_config_t wc = {};
    if (ssid[0] != '\0') {
        // Clamp explicitly so the intent is clear and the compiler can see the
        // write cannot be truncated.
        const size_t ssid_max = sizeof(wc.sta.ssid) - 1;   // 31
        const size_t pass_max = sizeof(wc.sta.password) - 1;
        memcpy(wc.sta.ssid, ssid, strnlen(ssid, ssid_max));
        memcpy(wc.sta.password, pass, strnlen(pass, pass_max));
        snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
        wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (ssid[0] != '\0') {
        // Name the target so a failed join is diagnosable from the console alone.
        ESP_LOGI(TAG, "joining '%s' (password %s)", ssid, pass[0] ? "set" : "empty");
    }

    // Modem sleep. Without it the radio stays in the receive chain permanently,
    // which costs more than the polling interval ever does: the interval decides how
    // often the radio has to wake, this decides whether it sleeps at all. The board
    // talks to the network for a second or two every few minutes, so the extra
    // latency is irrelevant.
    //
    // This was briefly blamed for a connection loop that turned out to be the
    // station's reconnect loop fighting the provisioning AP for the single radio
    // (see wifi_prov_suspend_station). With that fixed, MIN_MODEM holds a link.
    // MIN rather than MAX so a poll does not have to wait through several DTIM
    // intervals for the AP to forward to us.
    const esp_err_t ps = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (ps != ESP_OK) {
        ESP_LOGW(TAG, "could not enable WiFi modem sleep: %s", esp_err_to_name(ps));
    } else {
        ESP_LOGI(TAG, "WiFi modem sleep enabled");
    }

    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "no stored credentials; provisioning portal required");
    }
}

bool wifi_wait_connected(int timeout_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (!s_connected && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return s_connected;
}

wifi_status_t wifi_get_status()
{
    wifi_status_t st = {};
    st.connected = s_connected;
    snprintf(st.ssid, sizeof(st.ssid), "%s", s_ssid);
    snprintf(st.ip, sizeof(st.ip), "%s", s_ip);
    if (s_connected) {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) st.rssi = ap.rssi;
    }
    return st;
}
