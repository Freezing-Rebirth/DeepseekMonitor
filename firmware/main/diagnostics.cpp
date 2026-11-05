// Liveness and consumption telemetry: one line every 30 s, DEBUG builds only.
//
// This exists to answer two questions that the panel cannot: is the application
// still running (WiFi state, last poll, last repaint), and how much of the wall clock
// the CPU actually spent awake. The duty-cycle figure is the only direct measurement
// of whether light sleep is doing anything.

#include "diagnostics.h"

#include <cstdio>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_private/esp_clk.h>
#include <esp_timer.h>

#include "app_config.h"
#include "debug_config.h"
#include "wifi_prov.h"

static const char *TAG = "app";

void diagnostics_heartbeat(TickType_t last_ui, TickType_t last_api)
{
#if DEBUG_LOGS
    const wifi_status_t w = wifi_get_status();

    // The countdowns are logged with the heartbeat so the refresh schedule can be
    // watched directly instead of inferred from the panel. A deadline already passed
    // wraps to a huge TickType_t, so each is clamped rather than reported as ~4.29e6
    // seconds.
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
    // xTaskGetTickCount() is supplied by the sleep timer, which the power management
    // layer keeps in step with real time across light sleep. esp_timer_get_time() is
    // the hardware timer, which stops while the CPU is asleep. The ratio of the two
    // advances over the same interval is therefore the duty cycle: 100% means the CPU
    // never slept.
    //
    // The CPU cycle counter would measure this directly, but it is 32 bits, so at
    // 80 MHz it wraps every 54 seconds - less than the heartbeat interval - which is
    // how the first attempt at this reported nonsense.
    const int64_t us = esp_timer_get_time();
    static TickType_t last_hb_ticks = 0;
    static int64_t    last_hb_us    = 0;
    uint32_t duty = 0;
    bool duty_known = false;
    if (last_hb_ticks != 0) {
        const int64_t wall_us = (int64_t)(now_ticks - last_hb_ticks) *
                                (1000000LL / configTICK_RATE_HZ);
        int64_t hw_us = us - last_hb_us;
        // Clamp instead of discarding. The two clocks round against each other, so a
        // fully awake interval can overshoot by a tick or two; dropping those samples
        // is the same branch as a sleeping chip and reported awake=0% for three
        // heartbeats in a row with light sleep switched off.
        if (wall_us > 0) {
            if (hw_us < 0) hw_us = 0;
            if (hw_us > wall_us) hw_us = wall_us;
            duty = (uint32_t)(hw_us * 100 / wall_us);
            duty_known = true;
        }
    }
    last_hb_ticks = now_ticks;
    last_hb_us = us;

    // The first heartbeat has no baseline to compare against, so it says so rather
    // than reporting a duty cycle of zero.
    char duty_str[8];
    if (duty_known) snprintf(duty_str, sizeof(duty_str), "%u%%", (unsigned)duty);
    else            snprintf(duty_str, sizeof(duty_str), "--");

    ESP_LOGI(TAG, "alive: wifi=%d ip=%s rssi=%d cpu=%uMHz awake=%s psram=%u | next api in %us, ui in %us",
             (int)w.connected, w.ip, (int)w.rssi,
             (unsigned)(esp_clk_cpu_freq() / 1000000), duty_str,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)to_api, (unsigned)to_ui);
#else
    (void)last_ui;
    (void)last_api;
#endif
}
