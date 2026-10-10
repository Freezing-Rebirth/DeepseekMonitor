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
#include <esp_private/esp_clk.h>
#include <esp_cpu.h>
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
#include "debug_config.h"
#include "holidays.h"
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
#define FRAME_DUMP_ENABLED 0

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

// When the station first went down with credentials stored. Drives the automatic
// re-opening of the setup form, so a bad password recovers without a reboot.
static TickType_t s_offline_since = 0;

// How long to stay offline before offering the setup form again. Long enough that a
// router reboot or a brief dropout does not raise the access point unnecessarily.
#define PORTAL_AUTO_OPEN_MS (2 * 60 * 1000)

// Last battery reading that actually succeeded. Kept so a transient ADC failure
// does not blank the field the panel was already showing.
static battery_reading_t s_last_bat = {};

static void portal_task(void *arg);

// ---------------------------------------------------------------------------
// Holiday arrangement refresh.
//
// Two requests on different schedules, because they answer different questions:
//
//   daily  - the ~200 byte single-day endpoint, for the day actually being
//            displayed. This is what keeps up with an arrangement that is amended
//            after it is published, which is the point of checking at all.
//   monthly - the ~3 KB yearly table, so days the daily check cannot see - the next
//            switch time can fall on one - are still covered.
//
// Both write to NVS and both fall back to the compiled table, so no failure here
// can change what the panel shows; it only stops the board from learning.
//
// Its own task because each request is an HTTPS round trip and the main loop must
// not block on it. 12 KB of stack for the TLS handshake, same as the balance poll.
// ---------------------------------------------------------------------------
static void holiday_task(void *)
{
    while (true) {
        if (wifi_get_status().connected && clock_is_synced()) {
            struct tm bj;
            pricing_beijing_tm(clock_now(), &bj);
            const int year = bj.tm_year + 1900;
            const int month = bj.tm_mon + 1;
            const int day = bj.tm_mday;

            if (holidays_today_stale(year, month, day)) {
                holidays_fetch_today(year, month, day);
            }
            // A year the compiled table does not cover has to come from here, so
            // fetch it even when the monthly interval has not elapsed yet.
            if (holidays_year_refresh_due() || !holidays_year_supported(year)) {
                holidays_runtime_fetch_year(year);
            }
        }
        // Woken hourly so a board whose clock only just synced, or whose network was
        // down, retries promptly rather than waiting a whole day. Both fetches are
        // gated on their own intervals inside.
        vTaskDelay(pdMS_TO_TICKS(60 * 60 * 1000));
    }
}

// Run the portal to completion and return true when credentials came back.
// Must be called with s_portal_active already set and no portal task running.
static bool portal_run(bool wait_forever)
{
    wifi_prov_portal_clear_submitted();
    if (!wifi_prov_portal_start()) {
        s_portal_active = false;
        return false;
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

    const bool submitted = wifi_prov_portal_submitted();
    wifi_prov_portal_stop();
    s_portal_active = false;
    return submitted;
}

static void portal_task(void *)
{
    // Always wait indefinitely. The form is the only way to fix a bad WiFi
    // password or a moved router, and a timed window meant that a second attempt
    // could arrive after the server had already shut down - the page then loaded
    // into nothing. The portal is cheap to hold open: the station is suspended
    // while it runs, so it is not fighting the access point.
    if (!portal_run(true)) {
        s_portal_active = false;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "credentials received, rejoining network");
    // The driver is still holding the previous station config; the form wrote to
    // NVS only. Apply the new one before reconnecting, or this attempt targets the
    // old network and fails until the next reboot.
    wifi_apply_stored_credentials();
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_wifi_connect();
    if (wifi_wait_connected(25000)) {
        ESP_LOGI(TAG, "reconnected");
        if (clock_sync_sntp(15000)) ESP_LOGI(TAG, "clock synchronised");
        poll_balance();
    } else {
        // Wrong password or unreachable network. The main loop notices that the
        // station is down and raises the form again, so the user gets another
        // attempt without touching the board.
        ESP_LOGW(TAG, "could not join the new network; the setup form will reopen");
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

    // No stored network, or the stored one did not answer: raise the portal so
    // the user can enter WiFi and the API key from a phone.
    // 12 KB of stack because this task goes on to call poll_balance(), and the TLS
    // handshake plus certificate-bundle parse does not fit in 6 KB - at 6 KB it
    // tripped the FreeRTOS stack-overflow hook and rebooted the board in a loop.
    if (!online && !appcfg_has_wifi()) {
        ESP_LOGI(TAG, "no stored network; starting provisioning portal");
        s_portal_active = true;
        xTaskCreatePinnedToCore(portal_task, "portal", 12288, nullptr, 3, nullptr, 0);
    } else if (!online) {
        // Has credentials that will not connect: a wrong password, a moved router,
        // or a network that is simply down. Either way the user cannot fix it from
        // the panel, and the only recovery used to be a BOOT long press they would
        // have to know about. Raise the form immediately, in the background, so the
        // UI still comes up and the user can retry as often as they like.
        ESP_LOGW(TAG, "stored network did not connect; opening the setup portal");
        s_portal_active = true;
        xTaskCreatePinnedToCore(portal_task, "portal", 12288, nullptr, 3, nullptr, 0);
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

    // Keep the holiday arrangement current without a reflash. Started whenever the
    // link is up; the task itself decides whether a refresh is due.
    if (online) {
        xTaskCreatePinnedToCore(holiday_task, "holidays", 12288, nullptr, 2, nullptr, 0);
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

        // Auto-recover an unreachable network.
        //
        // If the board cannot reach the stored network there is nothing the user can
        // do from the panel, so the setup form is raised on its own. Without this,
        // a wrong WiFi password looks exactly like dead hardware: the board
        // associates, fails the handshake, retries forever, and never offers a way
        // in. The delay keeps a brief blip - a router reboot, say - from raising the
        // access point when the link is about to come back by itself.
        if (!s_portal_active && appcfg_has_wifi() &&
            !wifi_get_status().connected) {
            if (s_offline_since == 0) {
                s_offline_since = xTaskGetTickCount();
            } else if ((xTaskGetTickCount() - s_offline_since) >=
                       pdMS_TO_TICKS(PORTAL_AUTO_OPEN_MS)) {
                ESP_LOGW(TAG, "offline for %u s with stored credentials; "
                              "opening the setup portal",
                         (unsigned)(PORTAL_AUTO_OPEN_MS / 1000));
                s_portal_active = true;
                xTaskCreatePinnedToCore(portal_task, "portal", 12288, nullptr, 3,
                                        nullptr, 0);
                s_offline_since = 0;
            }
        } else if (wifi_get_status().connected) {
            s_offline_since = 0;
        }

        // Heartbeat: with DEBUG_LOGS on this proves the LVGL task and the main loop
        // are both alive and shows the countdowns; otherwise it only advances the
        // timer.
        if ((xTaskGetTickCount() - last_hb) >= pdMS_TO_TICKS(30000)) {
            last_hb = xTaskGetTickCount();
#if DEBUG_LOGS
            const wifi_status_t w = wifi_get_status();
            // The countdowns are logged with the heartbeat so the refresh schedule can
            // be watched directly instead of inferred from the panel. A deadline
            // already passed wraps to a huge TickType_t, so each is clamped rather
            // than reported as ~4.29e6 seconds.
            const TickType_t now_ticks = xTaskGetTickCount();
            const TickType_t ui_left  = pdMS_TO_TICKS(APP_UI_REFRESH_MS) -
                                        (now_ticks - last_ui);
            const TickType_t api_left = pdMS_TO_TICKS(APP_BALANCE_POLL_MS) -
                                        (now_ticks - last_api);
            const uint32_t to_api = (uint32_t)((api_left > pdMS_TO_TICKS(APP_BALANCE_POLL_MS))
                                                   ? 0 : api_left / pdMS_TO_TICKS(1000));
            const uint32_t to_ui  = (uint32_t)((ui_left > pdMS_TO_TICKS(APP_UI_REFRESH_MS))
                                                   ? 0 : ui_left / pdMS_TO_TICKS(1000));
            // How much of the wall time the CPU actually ran, rather than inferring it.
            //
            // xTaskGetTickCount() is supplied by the sleep timer, which the power
            // management layer keeps in step with real time across light sleep.
            // esp_timer_get_time() is the hardware timer, which stops while the CPU is
            // asleep. The ratio of the two advances over the same interval is therefore
            // the duty cycle: 100% means the CPU never slept.
            //
            // The CPU cycle counter would measure this directly, but it is 32 bits, so at
            // 80 MHz it wraps every 54 seconds - less than the heartbeat interval - which
            // is how the first attempt at this reported nonsense.
            const int64_t us = esp_timer_get_time();
            static TickType_t last_hb_ticks = 0;
            static int64_t    last_hb_us    = 0;
            uint32_t duty = 0;
            if (last_hb_ticks != 0) {
                const int64_t wall_us = (int64_t)(now_ticks - last_hb_ticks) *
                                        (1000000LL / configTICK_RATE_HZ);
                const int64_t hw_us = us - last_hb_us;
                if (wall_us > 0 && hw_us >= 0 && hw_us <= wall_us) {
                    duty = (uint32_t)(hw_us * 100 / wall_us);
                }
            }
            last_hb_ticks = now_ticks;
            last_hb_us = us;

            ESP_LOGI(TAG, "alive: wifi=%d ip=%s rssi=%d cpu=%uMHz awake=%u%% psram=%u | next api in %us, ui in %us",
                     (int)w.connected, w.ip, (int)w.rssi,
                     (unsigned)(esp_clk_cpu_freq() / 1000000), (unsigned)duty,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)to_api, (unsigned)to_ui);
#endif
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
#if DEBUG_LOGS
            // The cadence is logged so the configured interval can be confirmed
            // against the ledger's sample spacing.
            ESP_LOGI(TAG, "polling balance (interval %u s)",
                     (unsigned)(APP_BALANCE_POLL_MS / 1000));
#endif
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

            // Battery diagnostics: one line per repaint when DEBUG_LOGS is on, so the
            // ADC reading can be watched over time rather than inferred from the
            // panel. A failed read is always reported - that is an event, not noise.
            if (!bat.valid) {
                ESP_LOGW(TAG, "battery read failed");
            }
#if DEBUG_LOGS
            else {
                ESP_LOGI(TAG, "battery %.3f V -> %d%%", (double)bat.volts, bat.percent);
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
