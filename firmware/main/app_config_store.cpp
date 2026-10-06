#include "app_config_store.h"
#include "wifi_prov.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char *TAG_CFG = "cfg";

// Runtime configuration lives in one place so the UI never reads NVS directly.
static char s_api_key[160] = {0};
static bool s_have_key = false;

void app_config_init()
{
    s_have_key = appcfg_get_api_key(s_api_key, sizeof(s_api_key));
    if (s_have_key) {
        ESP_LOGI(TAG_CFG, "API key loaded (%u chars)", (unsigned)strlen(s_api_key));
    } else {
        ESP_LOGW(TAG_CFG, "no API key configured");
    }
}

bool app_config_api_key(char *out, size_t out_len)
{
    if (!s_have_key) return false;
    snprintf(out, out_len, "%s", s_api_key);
    return true;
}

void app_config_set_api_key(const char *key)
{
    appcfg_set_api_key(key);
    snprintf(s_api_key, sizeof(s_api_key), "%s", key ? key : "");
    s_have_key = key && key[0] != '\0';
}
