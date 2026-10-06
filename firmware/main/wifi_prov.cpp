#include "wifi_prov.h"
#include "app_config.h"

#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs.h>

static const char *TAG = "wifi";

#define CFG_NS "dscfg"

static bool s_connected = false;
static char s_ip[24] = "0.0.0.0";
static char s_ssid[40] = {0};
static int8_t s_rssi = 0;

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
    char s[33], p[64];
    return appcfg_get_wifi(s, sizeof(s), p, sizeof(p));
}

// ---------------------------------------------------------------------------
// WiFi events
// ---------------------------------------------------------------------------
static void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "disconnected, retrying in 2s");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_connected = true;
        ESP_LOGI(TAG, "got IP %s", s_ip);
    }
}

void wifi_start()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

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
