#include "deepseek_api.h"
#include "app_config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>
static const char *TAG = "dsapi";

#define RESP_CAP 2048

typedef struct {
    char  buf[RESP_CAP];
    size_t len;
    bool  overflow;
} resp_t;

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA: {
        resp_t *r = (resp_t *)evt->user_data;
        if (!r) break;
        if (r->len + evt->data_len >= RESP_CAP) {
            r->overflow = true;
            break;
        }
        memcpy(r->buf + r->len, evt->data, evt->data_len);
        r->len += evt->data_len;
        r->buf[r->len] = '\0';
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

static bool parse_balance_json(const char *json, size_t len, ds_balance_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        snprintf(out->error, sizeof(out->error), "JSON parse failed");
        return false;
    }

    const cJSON *avail = cJSON_GetObjectItemCaseSensitive(root, "is_available");
    if (cJSON_IsBool(avail)) {
        out->is_available = cJSON_IsTrue(avail);
    }

    const cJSON *infos = cJSON_GetObjectItemCaseSensitive(root, "balance_infos");
    if (!cJSON_IsArray(infos) || cJSON_GetArraySize(infos) == 0) {
        snprintf(out->error, sizeof(out->error), "balance_infos missing");
        cJSON_Delete(root);
        return false;
    }

    // Prefer the CNY entry; fall back to the first one.
    const cJSON *chosen = nullptr;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, infos) {
        const cJSON *cur = cJSON_GetObjectItemCaseSensitive(item, "currency");
        if (cJSON_IsString(cur) && strcmp(cur->valuestring, "CNY") == 0) {
            chosen = item;
            break;
        }
        if (!chosen) chosen = item;
    }
    if (!chosen) chosen = cJSON_GetArrayItem(infos, 0);

    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(chosen, "currency");
    if (cJSON_IsString(cur)) {
        snprintf(out->currency, sizeof(out->currency), "%s", cur->valuestring);
    }

    const cJSON *total = cJSON_GetObjectItemCaseSensitive(chosen, "total_balance");
    const cJSON *grant = cJSON_GetObjectItemCaseSensitive(chosen, "granted_balance");
    const cJSON *topup = cJSON_GetObjectItemCaseSensitive(chosen, "topped_up_balance");

    // Values arrive as strings, e.g. "148.52".
    if (cJSON_IsString(total)) out->total_balance     = strtof(total->valuestring, nullptr);
    else if (cJSON_IsNumber(total)) out->total_balance = (float)total->valuedouble;
    if (cJSON_IsString(grant)) out->granted_balance   = strtof(grant->valuestring, nullptr);
    if (cJSON_IsString(topup)) out->topped_up_balance = strtof(topup->valuestring, nullptr);

    cJSON_Delete(root);
    return true;
}

ds_balance_t ds_query_balance(const char *api_key, int timeout_ms)
{
    ds_balance_t out = {};
    out.http_status = 0;

    if (!api_key || api_key[0] == '\0') {
        snprintf(out.error, sizeof(out.error), "no API key configured");
        return out;
    }

    resp_t resp = {};
    char url[160];
    snprintf(url, sizeof(url), "%s%s", APP_DS_BASE_URL, APP_DS_BALANCE_PATH);

    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", api_key);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = timeout_ms;
    cfg.event_handler = http_event_cb;
    cfg.user_data = &resp;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = false;
    cfg.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        snprintf(out.error, sizeof(out.error), "http client init failed");
        return out;
    }

    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Accept", "application/json");

    const esp_err_t err = esp_http_client_perform(client);
    out.http_status = esp_http_client_get_status_code(client);

    if (err != ESP_OK) {
        snprintf(out.error, sizeof(out.error), "request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return out;
    }
    esp_http_client_cleanup(client);

    if (out.http_status != 200) {
        snprintf(out.error, sizeof(out.error), "HTTP %d", out.http_status);
        ESP_LOGW(TAG, "balance query HTTP %d, body: %.180s", out.http_status, resp.buf);
        return out;
    }
    if (resp.overflow) {
        snprintf(out.error, sizeof(out.error), "response too large");
        return out;
    }

    if (!parse_balance_json(resp.buf, resp.len, &out)) {
        return out;
    }

    out.ok = true;
    return out;
}
