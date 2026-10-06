// ---------------------------------------------------------------------------
// DeepSeek Monitor - application entry point.
//
// Waveshare ESP32-S3-RLCD-4.2 / ST7305 400x300 reflective monochrome panel.
//
// Data flow:
//   SNTP ---------> civil clock (Beijing time)
//   WiFi ---------> link status + RSSI
//   ADC GPIO4 ----> 18650 cell voltage
//   /user/balance -> account balance (polled every 10 minutes)
//   usage ledger -> burn rate + runway (derived on device; DeepSeek has no
//                   spending-history endpoint)
//   pricing.cpp --> peak / off-peak classification, which inverts the screen
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include "board_rlcd.h"
#include "battery.h"
#include "app_config.h"
#include "app_config_store.h"
#include "clock_time.h"
#include "deepseek_api.h"
#include "frame_dump.h"
#include "pricing.h"
#include "pricing_selftest.h"
#include "selftest.h"
#include "ui.h"
#include "usage_ledger.h"
#include "wifi_prov.h"
#include "wifi_portal.h"

static const char *TAG = "app";

// Set to 1 to run the raw panel bring-up pattern instead of the application UI.
#define BRINGUP_SELFTEST 0

// Set to 1 to log a week-long sweep of the tariff rules at boot, verifying that
// pricing_classify() and pricing_seconds_to_next_change() agree with the published
// peak windows. Development aid only; it emits ~200 lines.
#define PRICING_SELFTEST 0

// Set to 1 to print every battery reading with its raw ADC counts and pin voltage.
//
// The percentage on the panel comes from a straight line between 2.5 V and 4.2 V,
// which is a placeholder: a Li-ion cell is flat through the middle of its range, so
// a linear map is least accurate where it matters most. Measuring the real curve
// needs two numbers per point - what the ADC saw, and what a multimeter reads on
// the cell - so this mode prints the first. See the calibration section of the
// README.
#define BATTERY_REPORT_RAW 0

// Set to 1 to render the panel with fixed sample data instead of the live API
// result. The panel's dynamic paths - the balance figures, the token equivalent
// and the NEXT switch time - only run once a balance has been fetched and the
// clock has synced, so they cannot be checked on a freshly flashed board without
// a network. This switch exercises them offline.
#define DEMO_DATA 0

// Set to 1 to stream the 1bpp frame buffer over the console serial port so the
// rendered panel can be inspected pixel-exactly on a workstation. Development aid
// only: it adds a recurring serial burst. Turn on when capturing a frame with
// _research/capture_bin.py.
#define FRAME_DUMP_ENABLED 1

static RlcdPanel s_panel;

// Forward declaration: the provisioning task re-polls once the user has saved
// credentials, and poll_balance() is defined further down.
static void poll_balance();

// ---------------------------------------------------------------------------
// Provisioning
//
// The portal runs on its own task so the UI keeps rendering while the user
// fills in the form. It exits as soon as credentials arrive.
// ---------------------------------------------------------------------------
static volatile bool s_portal_active = false;

// Last battery reading that actually succeeded. Kept so a transient ADC failure
// does not blank the field the panel was already showing.
static battery_reading_t s_last_bat = {};

static void portal_task(void *arg)
{
    // First boot waits forever: the user has to find the AP, type a long API key
    // and press save, and a five-minute cap meant the server was already shut down
    // by the time they got there - the page then loaded empty because the port
    // accepted the connection and closed it. Re-entry from the BOOT button keeps a
    // timeout so a stray long press cannot pin the radio in AP mode forever.
    const bool wait_forever = (arg != nullptr);

    if (!wifi_prov_portal_start()) {
        s_portal_active = false;
        vTaskDelete(nullptr);
        return;
    }

    if (wait_forever) {
        ESP_LOGI(TAG, "portal waiting indefinitely for the setup form");
        while (!wifi_prov_portal_submitted()) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    } else {
        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5 * 60 * 1000);
        while (!wifi_prov_portal_submitted() && xTaskGetTickCount() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    wifi_prov_portal_stop();
    s_portal_active = false;

    if (wifi_prov_portal_submitted()) {
        ESP_LOGI(TAG, "credentials received, rejoining network");
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_wifi_connect();
        if (wifi_wait_connected(25000)) {
            ESP_LOGI(TAG, "reconnected");
            if (clock_sync_sntp(15000)) ESP_LOGI(TAG, "clock synchronised");
            poll_balance();
        } else {
            ESP_LOGW(TAG, "could not join the new network");
        }
    }

    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// LVGL tick + task
// ---------------------------------------------------------------------------
// LVGL 9 pulls its tick from a millisecond callback rather than being fed by
// the application, so it is wired straight to the esp_timer clock.
static uint32_t lvgl_tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------------------------------
// LVGL is built with CONFIG_LV_OS_NONE, so it has no internal locking. Two tasks
// touch it: lvgl_task runs lv_timer_handler() (which renders and flushes), and the
// main task runs ui_update() to set label text and alignment. Without a lock the
// main task corrupts LVGL's object lists while the timer task walks them, and the
// main task ends up stuck inside LVGL - the task watchdog then reports IDLE0
// starved. Every LVGL call from a task other than lvgl_task goes through this
// recursive mutex.
// ---------------------------------------------------------------------------
static SemaphoreHandle_t s_lvgl_mutex = nullptr;

static void lvgl_lock()
{
    if (s_lvgl_mutex) xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
}

static void lvgl_unlock()
{
    if (s_lvgl_mutex) xSemaphoreGiveRecursive(s_lvgl_mutex);
}

static void lvgl_task(void *)
{
    while (true) {
        lvgl_lock();
        uint32_t delay_ms = lv_timer_handler();
        lvgl_unlock();
        if (delay_ms < 10)  delay_ms = 10;
        if (delay_ms > 100) delay_ms = 100;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
typedef struct {
    bool   balance_valid;
    float  balance_cny;
    char   balance_currency[8];
    bool   ledger_valid;
    float  daily_burn;
    float  runway_days;
    bool   api_ok;
    char   status[48];
} app_state_t;

static app_state_t s_state = {};

static void poll_balance()
{
    char key[160];
    if (!app_config_api_key(key, sizeof(key))) {
        snprintf(s_state.status, sizeof(s_state.status), "NO API KEY");
        s_state.api_ok = false;
        return;
    }

    ESP_LOGI(TAG, "querying DeepSeek balance");
    const ds_balance_t bal = ds_query_balance(key, 15000);

    if (!bal.ok) {
        snprintf(s_state.status, sizeof(s_state.status), "API: %.32s", bal.error);
        s_state.api_ok = false;
        ESP_LOGW(TAG, "balance query failed: %s (http %d)", bal.error, bal.http_status);
        return;
    }

    s_state.api_ok = true;
    s_state.balance_valid = true;
    s_state.balance_cny = bal.total_balance;
    snprintf(s_state.balance_currency, sizeof(s_state.balance_currency), "%s",
             bal.currency[0] ? bal.currency : "CNY");
    s_state.status[0] = '\0';

    ESP_LOGI(TAG, "balance %.2f %s (granted %.2f, topped up %.2f, available=%d)",
             (double)bal.total_balance, s_state.balance_currency,
             (double)bal.granted_balance, (double)bal.topped_up_balance,
             (int)bal.is_available);

    const time_t now = clock_now();
    if (clock_is_synced()) {
        ledger_record(bal.total_balance, (int64_t)now);
        const ledger_stats_t st = ledger_stats(bal.total_balance, (int64_t)now);
        s_state.ledger_valid = st.valid;
        s_state.daily_burn = st.daily_burn_cny;
        s_state.runway_days = st.runway_days;
        ESP_LOGI(TAG, "ledger: %d samples over %lld s, burn %.2f/Day, runway %.1f days",
                 ledger_sample_count(), (long long)ledger_window_sec(),
                 (double)st.daily_burn_cny, (double)st.runway_days);
    }
}

static void build_ui_model(ui_model_t *m)
{
    memset(m, 0, sizeof(*m));

    m->clock_valid = clock_is_synced();
    m->now = clock_now();

    const wifi_status_t w = wifi_get_status();
    m->wifi_connected = w.connected;
    m->wifi_rssi = w.rssi;

    m->balance_valid = s_state.balance_valid;
    m->balance_cny = s_state.balance_cny;
    snprintf(m->balance_currency, sizeof(m->balance_currency), "%s",
             s_state.balance_currency[0] ? s_state.balance_currency : "CNY");

    m->ledger_valid = s_state.ledger_valid;
    m->daily_burn_cny = s_state.daily_burn;
    m->runway_days = s_state.runway_days;

    m->model = APP_DEFAULT_MODEL;
    m->tier = APP_DEFAULT_TIER;
    m->api_ok = s_state.api_ok;
    snprintf(m->status_text, sizeof(m->status_text), "%s",
             s_state.status[0] ? s_state.status : "WAITING FOR DATA");

#if DEMO_DATA
    // Fixed sample data. The balance is chosen so the token line comes out at
    // 1.06B: 148.52 CNY / 0.14 CNY per 1M tokens = 1060.86M tokens.
    m->clock_valid = true;
    m->now = 1779156000;            // 2026-05-19 10:00 Beijing (Tue), PEAK window
    m->wifi_connected = true;
    m->battery_valid = true;
    m->battery_percent = 94;
    m->balance_valid = true;
    m->balance_cny = 148.52f;
    m->ledger_valid = true;
    m->daily_burn_cny = 3.86f;
    m->runway_days = 38.0f;
#endif
}

// ---------------------------------------------------------------------------
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "DeepSeek Monitor starting");
    ESP_LOGI(TAG, "PSRAM free at boot: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // ---- panel + LVGL ----
    err = s_panel.Init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
        return;
    }

    // ---- tariff rules ----
#if PRICING_SELFTEST
    pricing_selftest();
#endif

#if BRINGUP_SELFTEST
    selftest_run(s_panel);
#else
    lv_tick_set_cb(lvgl_tick_get_cb);

    // ---- configuration, sensors ----
    app_config_init();
    ledger_init();
    battery_init();

    // ---- network ----
    wifi_start();
    bool online = wifi_wait_connected(20000);

    // No stored network, or the stored one did not answer: raise the portal so
    // the user can enter WiFi and the API key from a phone.
    if (!online && !appcfg_has_wifi()) {
        ESP_LOGI(TAG, "no stored network; starting provisioning portal");
        s_portal_active = true;
        // Non-null argument: wait forever, this is first-time setup. 12 KB of stack
        // because this task goes on to call poll_balance(), and the TLS handshake
        // plus certificate-bundle parse does not fit in 6 KB - at 6 KB it tripped
        // the FreeRTOS stack-overflow hook and rebooted the board in a loop.
        xTaskCreatePinnedToCore(portal_task, "portal", 12288, (void *)1, 3, nullptr, 0);
    }

    if (online) {
        ESP_LOGI(TAG, "WiFi connected");
        if (clock_sync_sntp(15000)) {
            ESP_LOGI(TAG, "clock synchronised");
        }
    } else {
        ESP_LOGW(TAG, "no WiFi; continuing offline");
    }

    if (online && clock_is_synced()) {
        poll_balance();
    }

    // ---- UI ----
    // The mutex must exist before any LVGL call and before the timer task starts.
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mutex) {
        ESP_LOGE(TAG, "could not create the LVGL mutex");
        return;
    }

    lvgl_lock();
    ui_init(s_panel.display());
    ui_model_t model;
    build_ui_model(&model);
    ui_update(&model);
    lvgl_unlock();

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, nullptr, 2, nullptr, 0);

#if FRAME_DUMP_ENABLED
    frame_dump_start(s_panel);
#endif

    // ---- main loop ----
    TickType_t last_ui = xTaskGetTickCount();
    TickType_t last_api = xTaskGetTickCount();
    TickType_t last_hb = xTaskGetTickCount();
    price_state_t last_price_state = pricing_classify(model.now);

    while (true) {
        // Sleep until the next scheduled event instead of polling every second.
        //
        // The loop used to wake once a second purely to compare tick counters,
        // which keeps the CPU out of idle for no reason. Nothing here needs
        // sub-second resolution: the clock changes once a minute and the balance
        // every few minutes. The floor of 250 ms is what keeps the BOOT button
        // responsive, since that is the only input.
        {
            const TickType_t now = xTaskGetTickCount();
            const TickType_t ui_left  = pdMS_TO_TICKS(APP_UI_REFRESH_MS) -
                                        (now - last_ui);
            const TickType_t api_left = pdMS_TO_TICKS(APP_BALANCE_POLL_MS) -
                                        (now - last_api);
            // TickType_t is unsigned, so a deadline already passed wraps to a huge
            // value - take the minimum, then clamp it down to the floor.
            TickType_t wait = (ui_left < api_left) ? ui_left : api_left;
            if (wait > pdMS_TO_TICKS(250)) {
                vTaskDelay(wait);
            } else {
                vTaskDelay(pdMS_TO_TICKS(250));
            }
        }

        // Heartbeat: proves the LVGL task and the main loop are both alive.
        if ((xTaskGetTickCount() - last_hb) >= pdMS_TO_TICKS(30000)) {
            last_hb = xTaskGetTickCount();
            const wifi_status_t w = wifi_get_status();
            // The countdowns are logged with the heartbeat so the refresh schedule
            // can be watched directly instead of inferred from the panel.
            const uint32_t to_api = (uint32_t)((pdMS_TO_TICKS(APP_BALANCE_POLL_MS) -
                                    (xTaskGetTickCount() - last_api)) / pdMS_TO_TICKS(1000));
            const uint32_t to_ui = (uint32_t)((pdMS_TO_TICKS(APP_UI_REFRESH_MS) -
                                    (xTaskGetTickCount() - last_ui)) / pdMS_TO_TICKS(1000));
            ESP_LOGI(TAG, "alive: wifi=%d ip=%s rssi=%d psram=%u | next api in %us, ui in %us",
                     (int)w.connected, w.ip, (int)w.rssi,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)to_api, (unsigned)to_ui);
        }

        price_state_t now_state = last_price_state;
        if (clock_is_synced()) {
            now_state = pricing_classify(clock_now());
        }

        const bool state_changed = (now_state != last_price_state);
        const bool ui_due = (xTaskGetTickCount() - last_ui) >= pdMS_TO_TICKS(APP_UI_REFRESH_MS);
        const bool api_due = (xTaskGetTickCount() - last_api) >= pdMS_TO_TICKS(APP_BALANCE_POLL_MS);

        // Hold BOOT for ~3 s at any time to re-enter provisioning.
        if (!s_portal_active && boot_button_long_pressed()) {
            ESP_LOGI(TAG, "BOOT long press: starting provisioning portal");
            s_portal_active = true;
            // Null argument: bounded wait, so a stray press cannot pin AP mode on.
            // Same 12 KB as the first-boot portal, which also ends in poll_balance().
            xTaskCreatePinnedToCore(portal_task, "portal", 12288, nullptr, 3, nullptr, 0);
        }

        // (The old "battery once a minute" counter here was dead code: it
        // incremented and reset without gating anything. The battery is sampled
        // with the rest of the UI every APP_UI_REFRESH_MS, which is already 60 s.)

        if (api_due) {
            last_api = xTaskGetTickCount();
            // The cadence is logged so the configured interval can be confirmed
            // against the ledger's sample spacing.
            ESP_LOGI(TAG, "polling balance (interval %u s)",
                     (unsigned)(APP_BALANCE_POLL_MS / 1000));
            if (wifi_get_status().connected) {
                if (!clock_is_synced()) clock_sync_sntp(10000);
                poll_balance();
            } else {
                snprintf(s_state.status, sizeof(s_state.status), "NO WIFI");
                s_state.api_ok = false;
            }
        }

        if (ui_due || state_changed || api_due) {
            last_ui = xTaskGetTickCount();
            last_price_state = now_state;

            // Battery: read here, but only publish a reading that succeeded. A
            // failed ADC read used to overwrite the field with valid=false, which
            // made the panel drop to "BAT:--" even though it had shown a number a
            // second earlier. Keep the last good value instead.
            const battery_reading_t bat = battery_read();
            if (bat.valid) {
                s_last_bat = bat;
            }

            ui_model_t m;
            build_ui_model(&m);
            m.battery_valid = s_last_bat.valid;
            m.battery_volts = s_last_bat.volts;
            m.battery_percent = s_last_bat.percent;
            lvgl_lock();
            ui_update(&m);
            ui_set_heartbeat(true);
            lvgl_unlock();

            // Battery diagnostics: one line per repaint, so the ADC reading can be
            // watched over time rather than inferred from the panel.
            if (bat.valid) {
                ESP_LOGI(TAG, "battery %.3f V -> %d%%", (double)bat.volts, bat.percent);
#if BATTERY_REPORT_RAW
                // Everything needed to fit the divider ratio and the discharge
                // curve: what the ADC counted, what the calibration scheme made of
                // it, and what that implies for the cell. Note the cell voltage in
                // a terminal beside it.
                ESP_LOGI(TAG, "  raw=%d counts, pin=%d mV, divider=%d -> cell=%.3f V",
                         bat.raw, bat.pin_mv, (int)BAT_DIVIDER, (double)bat.volts);
#endif
            } else {
                ESP_LOGW(TAG, "battery read failed");
            }

            // Blink the header mark so a live screen is distinguishable from a
            // frozen one without a serial console. (Done inside the lock above.)

            if (state_changed) {
                ESP_LOGI(TAG, "tariff state -> %s (repaint forced)", pricing_state_name(now_state));
            }
            ESP_LOGI(TAG, "ui refreshed, free PSRAM %u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
    }
#endif
}
