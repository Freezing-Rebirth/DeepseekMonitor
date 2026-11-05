// ---------------------------------------------------------------------------
// DeepSeek Monitor - application entry point.
//
// Waveshare ESP32-S3-RLCD-4.2 / ST7305 400x300 reflective monochrome panel.
//
// Data flow:
//   SNTP ---------> civil clock (Beijing time)
//   WiFi ---------> link status + RSSI
//   ADC GPIO4 ----> 18650 cell voltage
//   /user/balance -> account balance (polled every three minutes)
//   usage ledger -> burn rate + runway (derived on device; DeepSeek has no
//                   spending-history endpoint)
//   pricing.cpp --> peak / off-peak classification, which inverts the screen
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_private/esp_clk.h>
#include <esp_app_desc.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <nvs_flash.h>

#include "app_config.h"
#include "app_config_store.h"
#include "balance_monitor.h"
#include "battery.h"
#include "board_rlcd.h"
#include "clock_time.h"
#include "debug_config.h"
#include "diagnostics.h"
#include "frame_dump.h"
#include "holidays.h"
#include "panel_model.h"
#include "power.h"
#include "pricing.h"
#include "provisioning.h"
#include "selftest.h"
#include "ui.h"
#include "usage_ledger.h"
#include "wifi_prov.h"

#if PRICING_SELFTEST
#include "pricing_selftest.h"
#endif

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
#define FRAME_DUMP_ENABLED 0

static RlcdPanel s_panel;

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
    // On a reflective panel with no animation running there is nothing to redraw, and
    // lv_timer_handler() reports 0 in that case. It used to be floored to 10 ms,
    // which is 100 wakeups a second - about 8.6 million a day - purely to conclude
    // that nothing had changed. On a battery-powered board that floor is one of the
    // larger standing loads, so the idle period is a named constant rather than a
    // magic number: raise it to trade a little latency on a value change for a
    // longer runtime between charges.
    while (true) {
        lvgl_lock();
        uint32_t delay_ms = lv_timer_handler();
        lvgl_unlock();
        if (delay_ms < APP_LVGL_IDLE_MS)  delay_ms = APP_LVGL_IDLE_MS;
        if (delay_ms > APP_LVGL_MAX_MS)   delay_ms = APP_LVGL_MAX_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// ---------------------------------------------------------------------------
extern "C" void app_main(void)
{
    // The single line this firmware prints at boot, and the reason it is WARN rather
    // than INFO: CONFIG_LOG_DEFAULT_LEVEL is WARN in release, and telling one build
    // from another is the most useful thing the console ever says. (The old line here
    // raised the level of tag "app_init", which no translation unit uses, so the
    // banner it was meant to enable never printed.)
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGW(TAG, "DeepSeek Monitor %s (built %s %s)", desc->version, desc->date, desc->time);

    // ---- power ----
    power_configure();

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
    // What the last session stored, before this one has read anything: the boot paint
    // uses it, and battery.cpp's gauge uses it to recognise a charger that is already
    // connected - the terminal is lifted, so this session's first reading arrives high.
    battery_reading_t stored = {};
    if (!battery_restore(&stored)) {
        stored = {};
    }

    // Read the cell once here, before wifi_start(). The ADC is not reliably ours after
    // the radio and power management are up: conversions then come back
    // ESP_ERR_TIMEOUT because the ADC1 lock is held elsewhere, and they keep doing so
    // for the rest of the session - measured on this board, every repaint from five
    // seconds onwards. This early read is what fills the cache the panel then repaints
    // from; if even it fails, the stored value above keeps a number on the panel. See
    // battery.cpp.
    const battery_reading_t at_boot = battery_read();
    if (!at_boot.valid && stored.valid) {
        ESP_LOGW(TAG, "battery ADC unavailable at boot; showing the last stored %d%% (%.3f V)",
                 stored.percent, (double)stored.volts);
    }
    // Load any previously fetched holiday arrangement before the first tariff
    // classification, so a reboot keeps using last month's data rather than falling
    // back to the compiled table for a year it does not cover.
    holidays_runtime_init();
#if HOLIDAYS_FORCE_REFRESH
    // Development aid: see debug_config.h.
    holidays_force_refresh_once();
#endif

    // ---- network ----
    wifi_start();
    bool online = wifi_wait_connected(20000);

    // Raise the setup form if there is no stored network, or if the stored one did not
    // answer. Called here rather than from the loop so it is up before the first paint.
    provisioning_after_wifi(online);

    if (online) {
        ESP_LOGI(TAG, "WiFi connected");
        if (clock_sync_sntp(15000)) {
            ESP_LOGI(TAG, "clock synchronised");
        }
    } else {
        ESP_LOGW(TAG, "no WiFi; continuing offline");
    }

    if (online && clock_is_synced()) {
        balance_poll();
    }

    // Keep the holiday arrangement current without a reflash. The task itself decides
    // whether a refresh is due.
    if (online) {
        holidays_start_refresh_task();
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
    panel_model_build(&model);
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
    const TickType_t boot_ticks = xTaskGetTickCount();
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

        // Setup-portal policy: reopen the form after a sustained dropout, and on a BOOT
        // long press.
        provisioning_tick();

        // Heartbeat: proves both tasks alive and reports the CPU duty cycle. Its body
        // is compiled out unless DEBUG_LOGS is on.
        if ((xTaskGetTickCount() - last_hb) >= pdMS_TO_TICKS(30000)) {
            last_hb = xTaskGetTickCount();
            diagnostics_heartbeat(last_ui, last_api);
        }

        price_state_t now_state = last_price_state;
        if (clock_is_synced()) {
            now_state = pricing_classify(clock_now());
        }

        const bool state_changed = (now_state != last_price_state);
        const bool ui_due = (xTaskGetTickCount() - last_ui) >= pdMS_TO_TICKS(APP_UI_REFRESH_MS);
        const bool api_due = (xTaskGetTickCount() - last_api) >= pdMS_TO_TICKS(APP_BALANCE_POLL_MS);

        // Boot repair: a few seconds in, re-send the panel init once (see RlcdPanel::Repair).
        // An ST7305 that came up dark never accepts a frame and nothing else in the application
        // can tell, so this is the one chance to catch it. It is a no-op for a healthy panel.
        {
            static bool panel_repaired = false;
            if (!panel_repaired && (xTaskGetTickCount() - boot_ticks) >= pdMS_TO_TICKS(4000)) {
                panel_repaired = true;
                lvgl_lock();
                s_panel.Repair();
                lvgl_unlock();
                ui_model_t m;
                panel_model_build(&m);
                lvgl_lock();
                ui_update(&m);
                lvgl_unlock();
            }
        }

        if (api_due) {
            last_api = xTaskGetTickCount();
#if DEBUG_LOGS
            // The cadence is logged so the configured interval can be confirmed
            // against the ledger's sample spacing.
            ESP_LOGI(TAG, "polling balance (interval %u s)",
                     (unsigned)(APP_BALANCE_POLL_MS / 1000));
#endif
            if (wifi_get_status().connected) {
                if (!clock_is_synced()) clock_sync_sntp(10000);
                balance_poll();
            }
        }

        if (ui_due || state_changed || api_due) {
            last_ui = xTaskGetTickCount();
            last_price_state = now_state;

            // battery_read() keeps its last successful reading, so a failed conversion
            // no longer blanks a number the panel was already showing.
            const battery_reading_t bat = battery_read();

            ui_model_t m;
            panel_model_build(&m);
            lvgl_lock();
            ui_update(&m);
            lvgl_unlock();

            // Battery diagnostics: one line per repaint when DEBUG_LOGS is on, so the
            // ADC reading can be watched over time rather than inferred from the
            // panel. A failed read is always reported - that is an event, not noise.
            if (!bat.valid) {
                ESP_LOGW(TAG, "battery read failed");
            }
#if DEBUG_LOGS
            else {
                ESP_LOGI(TAG, "battery %.3f V: terminal %d%%, published %d%%",
                         (double)bat.volts, bat.raw_percent, bat.percent);
#if BATTERY_REPORT_RAW
                // Everything needed to fit the divider ratio and the discharge
                // curve: what the ADC counted, what the calibration scheme made of
                // it, and what that implies for the cell. Note the cell voltage in
                // a terminal beside it.
                ESP_LOGI(TAG, "  raw=%d counts, pin=%d mV, divider=%d -> cell=%.3f V",
                         bat.raw, bat.pin_mv, (int)BAT_DIVIDER, (double)bat.volts);
#endif
            }
#endif

            if (state_changed) {
                ESP_LOGI(TAG, "tariff state -> %s (repaint forced)", pricing_state_name(now_state));
            }
#if DEBUG_LOGS
            ESP_LOGI(TAG, "ui refreshed, free PSRAM %u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
        }
    }
#endif
}
