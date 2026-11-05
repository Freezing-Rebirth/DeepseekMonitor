#include "deepseek_api.h"
#include "app_config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include "balance_json.h"
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

    if (!balance_json_parse(resp.buf, resp.len, &out)) {
        return out;
    }

    out.ok = true;
    return out;
}
