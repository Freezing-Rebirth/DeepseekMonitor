#include "wifi_portal.h"
#include "wifi_prov.h"
#include "app_config_store.h"
#include "app_config.h"
#include "board_rlcd.h"

#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <driver/gpio.h>

static const char *TAG = "portal";

static httpd_handle_t s_server = nullptr;
static esp_netif_t   *s_ap_netif = nullptr;
static volatile bool  s_submitted = false;
static bool           s_running = false;

// ---------------------------------------------------------------------------
// Setup form.
//
// Served as ONE buffer in a single response. It used to be sent as five
// httpd_resp_send_chunk() calls, which is the most likely reason the page arrived
// without its fields: a chunked response that the browser or an intermediate
// captive-portal prompt mishandles renders as a bare heading with no controls.
// One Content-Length-delimited body has no such failure mode.
// ---------------------------------------------------------------------------
static const char kPageHead[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>DeepSeek Monitor</title><style>"
    "body{font-family:monospace;background:#111;color:#eee;padding:18px;max-width:440px;margin:auto}"
    "h1{font-size:17px;border-bottom:2px solid #eee;padding-bottom:6px}"
    "label{display:block;margin-top:14px;font-size:13px}"
    "input{display:block;width:100%;box-sizing:border-box;padding:10px;margin-top:4px;"
    "background:#000;color:#eee;border:2px solid #eee;font-family:monospace;font-size:15px}"
    "button{display:block;margin-top:20px;width:100%;padding:12px;background:#eee;color:#000;"
    "border:0;font-family:monospace;font-weight:bold;font-size:15px}"
    "p{font-size:12px;opacity:.75;line-height:1.5}</style></head><body>"
    "<h1>DEEPSEEK MONITOR SETUP</h1>"
    "<form method=\"POST\" action=\"/save\">"
    "<label for=\"ssid\">WiFi SSID (2.4 GHz)</label>"
    "<input id=\"ssid\" name=\"ssid\" maxlength=\"32\" required value=\"";

static const char kPageMid[] =
    "\">"
    "<label for=\"pass\">WiFi password</label>"
    "<input id=\"pass\" name=\"pass\" type=\"password\" maxlength=\"63\" autocomplete=\"off\">"
    "<label for=\"key\">DeepSeek API key</label>"
    "<input id=\"key\" name=\"key\" type=\"text\" placeholder=\"sk-...\" maxlength=\"120\" "
    "autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\">"
    "<button type=\"submit\">SAVE AND CONNECT</button>"
    "</form>"
    "<p>Credentials are stored in the device's NVS flash partition. "
    "The access point stays up until the station connects.</p>"
    "</body></html>";

// Escape a value for an HTML attribute: the SSID comes from the user and may
// contain a quote or an ampersand, which would otherwise break out of value="".
static void html_attr_escape(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 7 < out_len; p++) {
        switch (*p) {
            case '&':  memcpy(out + o, "&amp;", 5);  o += 5; break;
            case '"':  memcpy(out + o, "&quot;", 6); o += 6; break;
            case '<':  memcpy(out + o, "&lt;", 4);   o += 4; break;
            case '>':  memcpy(out + o, "&gt;", 4);   o += 4; break;
            case '\'': memcpy(out + o, "&#39;", 5);  o += 5; break;
            default:   out[o++] = *p; break;
        }
    }
    out[o] = '\0';
}

static esp_err_t root_get(httpd_req_t *req)
{
    char ssid[40] = {0};
    char pass[80] = {0};
    char ssid_esc[256] = {0};
    appcfg_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
    html_attr_escape(ssid, ssid_esc, sizeof(ssid_esc));

    const size_t need = strlen(kPageHead) + strlen(ssid_esc) + strlen(kPageMid) + 1;
    char *page = (char *)malloc(need);
    if (!page) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }
    snprintf(page, need, "%s%s%s", kPageHead, ssid_esc, kPageMid);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_send(req, page, (ssize_t)strlen(page));
    free(page);
    return err;
}

// Percent-decode application/x-www-form-urlencoded values in place.
static void url_decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '+') { *o++ = ' '; s++; }
        else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(hex, nullptr, 16);
            s += 3;
        } else {
            *o++ = *s++;
        }
    }
    *o = '\0';
}

static void form_get(const char *body, const char *key, char *out, size_t out_len)
{
    out[0] = '\0';
    const size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *end = strchr(v, '&');
            size_t n = end ? (size_t)(end - v) : strlen(v);
            if (n >= out_len) n = out_len - 1;
            memcpy(out, v, n);
            out[n] = '\0';
            url_decode(out);
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[512] = {0};
    const int total = req->content_len;
    if (total <= 0 || total >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    int got = 0;
    while (got < total) {
        const int r = httpd_req_recv(req, body + got, total - got);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
            return ESP_FAIL;
        }
        got += r;
    }
    body[got] = '\0';

    char ssid[40] = {0};
    char pass[80] = {0};
    char key[160] = {0};
    form_get(body, "ssid", ssid, sizeof(ssid));
    form_get(body, "pass", pass, sizeof(pass));
    form_get(body, "key", key, sizeof(key));

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
        return ESP_FAIL;
    }

    // Validate the password shape before storing it.
    //
    // A WPA2 passphrase is either empty (open network), at least 8 characters, or
    // exactly 64 hex digits (a raw PSK). Anything else cannot work, and storing it
    // produces a device that associates, fails the four-way handshake with reason
    // 15, and retries forever - a symptom that looks like a hardware fault but is
    // really one short password. Catching it here turns that into a form error.
    const size_t pass_len = strlen(pass);
    bool pass_ok = (pass_len == 0) || (pass_len >= 8 && pass_len <= 63) ||
                   (pass_len == 64);
    if (pass_ok && pass_len == 64) {
        for (size_t i = 0; i < 64; i++) {
            const char c = pass[i];
            const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                             (c >= 'A' && c <= 'F');
            if (!hex) { pass_ok = false; break; }
        }
    }
    if (!pass_ok) {
        ESP_LOGW(TAG, "rejected a %u-character passphrase for '%s'",
                 (unsigned)pass_len, ssid);
        static const char bad[] =
            "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<style>body{font-family:monospace;background:#111;color:#eee;padding:24px}"
            "h1{font-size:16px}a{color:#8cf}</style></head><body>"
            "<h1>PASSWORD NOT ACCEPTED</h1>"
            "<p>A WPA2 passphrase must be at least 8 characters, or exactly 64 "
            "hexadecimal digits. Leave it empty for an open network.</p>"
            "<p><a href=\"/\">Go back</a></p></body></html>";
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_send(req, bad, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    appcfg_set_wifi(ssid, pass);
    if (key[0] != '\0') app_config_set_api_key(key);

    ESP_LOGI(TAG, "stored credentials for '%s' (passphrase %u chars, api key %s)",
             ssid, (unsigned)pass_len, key[0] ? "set" : "unchanged");

    static const char ok[] =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<style>body{font-family:monospace;background:#111;color:#eee;padding:24px}"
        "h1{font-size:16px}</style></head><body>"
        "<h1>SAVED</h1><p>Connecting to the network. "
        "This page can be closed.</p></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, ok, HTTPD_RESP_USE_STRLEN);

    s_submitted = true;
    return ESP_OK;
}

// Anything that is not the form or a save goes back to the form. The wildcard has
// to cover every method: registering it for GET alone let a stray POST (or a
// browser probe) fall through with no handler.
static esp_err_t catch_all(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

bool wifi_prov_portal_start()
{
    if (s_running) return true;

    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_ap_netif) {
            ESP_LOGE(TAG, "failed to create AP netif");
            return false;
        }
    }

    wifi_config_t ap = {};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", APP_PROV_AP_SSID);
    ap.ap.ssid_len = (uint8_t)strlen(APP_PROV_AP_SSID);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 6;
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server");
        return false;
    }

    // "/" and "/save" are registered BEFORE the catch-all. The catch-all covers
    // only the paths that the form does not use, so it cannot shadow them.
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get, .user_ctx = nullptr};
    const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post, .user_ctx = nullptr};
    const httpd_uri_t any  = {.uri = "/*", .method = HTTP_GET, .handler = catch_all, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &save);
    httpd_register_uri_handler(s_server, &any);

    s_running = true;
    ESP_LOGI(TAG, "provisioning portal up: join '%s' and open http://192.168.4.1/",
             APP_PROV_AP_SSID);

    // Freeze the station side while the form is being served. The mode is APSTA, so
    // leaving the station in its reconnect loop makes it fight the access point for
    // the one radio - which is how a submitted form ended up never reaching the
    // device at all.
    wifi_prov_suspend_station(true);
    return true;
}

bool wifi_prov_portal_submitted() { return s_submitted; }

void wifi_prov_portal_clear_submitted() { s_submitted = false; }

void wifi_prov_portal_stop()
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = nullptr;
    }
    if (s_running) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    }
    s_running = false;
    // Hand the station back so the caller can reconnect with whatever credentials
    // are now stored.
    wifi_prov_suspend_station(false);
    ESP_LOGI(TAG, "provisioning portal stopped");
}

// ---------------------------------------------------------------------------
// BOOT button (GPIO0, active low) long-press detector
// ---------------------------------------------------------------------------
bool boot_button_long_pressed()
{
    static bool     initialised = false;
    static bool     was_down = false;
    static uint32_t down_ms = 0;

    if (!initialised) {
        gpio_config_t io = {};
        io.pin_bit_mask = (1ULL << BTN_BOOT_PIN);
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io);
        initialised = true;
        return false;
    }

    const bool down = (gpio_get_level(BTN_BOOT_PIN) == 0);
    const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (down && !was_down) {
        down_ms = now;
    } else if (down && was_down) {
        if (now - down_ms > 3000) {
            was_down = down;
            down_ms = now;   // do not retrigger while held
            return true;
        }
    }
    was_down = down;
    return false;
}
