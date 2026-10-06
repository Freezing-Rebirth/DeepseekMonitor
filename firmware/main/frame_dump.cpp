#include "frame_dump.h"

#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char *TAG = "dump";

#define DUMP_MARKER_BEGIN "===FRAMEDUMP-BEGIN==="
#define DUMP_MARKER_END   "===FRAMEDUMP-END==="

// Stream the frame as plain hex. 32 bytes per line keeps the console happy and
// makes the payload easy to slice out of a captured log.
static void dump_task(void *arg)
{
    RlcdPanel *panel = static_cast<RlcdPanel *>(arg);

    // Let the UI settle before the first capture.
    vTaskDelay(pdMS_TO_TICKS(3000));

    while (true) {
        const uint8_t *fb = panel->frame_buffer();
        const size_t   n  = panel->frame_bytes();

        printf("\n%s\n", DUMP_MARKER_BEGIN);
        for (size_t i = 0; i < n; i += 32) {
            const size_t line = (n - i < 32) ? (n - i) : 32;
            for (size_t j = 0; j < line; j++) {
                printf("%02x", fb[i + j]);
            }
            printf("\n");
        }
        printf("%s\n", DUMP_MARKER_END);
        fflush(stdout);

        ESP_LOGI(TAG, "frame dumped (%u bytes)", (unsigned)n);
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

void frame_dump_start(RlcdPanel &panel)
{
    ESP_LOGI(TAG, "frame dump enabled: %u bytes per capture, every 60 s",
             (unsigned)panel.frame_bytes());
    xTaskCreatePinnedToCore(dump_task, "framedump", 4096, &panel, 1, nullptr, 1);
}
