#include "clock_time.h"

#include <string.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_sntp.h>

static const char *TAG = "clock";
static volatile bool s_synced = false;

static void sntp_sync_cb(struct timeval *tv)
{
    s_synced = true;
    ESP_LOGI(TAG, "SNTP synchronised: %lld", (long long)tv->tv_sec);
}

bool clock_sync_sntp(int timeout_ms)
{
    if (esp_sntp_enabled()) {
        // already running
    } else {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "cn.pool.ntp.org");
        esp_sntp_setservername(2, "time.cloudflare.com");
        esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
        esp_sntp_init();
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (!s_synced && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (!s_synced) {
        ESP_LOGW(TAG, "SNTP not synchronised within %d ms", timeout_ms);
        return false;
    }

    // The panel always reports Beijing time because the published DeepSeek
    // windows are Beijing time.
    setenv("TZ", "CST-8", 1);
    tzset();
    return true;
}

bool clock_is_synced() { return s_synced; }

time_t clock_now()
{
    time_t now = 0;
    time(&now);
    return now;
}

void clock_format_hhmm_cst(char *out, size_t out_len)
{
    const time_t now = clock_now();
    struct tm bj = {};
    const time_t bj_secs = now + 8 * 3600;
    gmtime_r(&bj_secs, &bj);
    snprintf(out, out_len, "%02d:%02d CST", bj.tm_hour, bj.tm_min);
}
