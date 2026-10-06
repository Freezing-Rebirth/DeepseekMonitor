#include "board_rlcd.h"
#include "selftest.h"

#include <esp_log.h>

static const char *TAG = "selftest";

// ---------------------------------------------------------------------------
// Panel bring-up pattern, written straight into the 1bpp frame buffer so it
// exercises the driver without LVGL in the loop.
//
// Reading the result:
//   * filled square in the TOP-LEFT corner          -> origin is correct
//   * filled square in the BOTTOM-RIGHT corner      -> both axes run correctly
//   * 2-pixel border around the whole panel         -> full frame reaches RAM
//   * checkered strip along the top                 -> bit packing is right
//   * no mirrored or rotated text-like artefacts    -> no axis swap
// ---------------------------------------------------------------------------
void selftest_run(RlcdPanel &panel)
{
    ESP_LOGI(TAG, "drawing panel bring-up pattern");

    // White background (1 = lit = white on this panel).
    panel.FillScreen(true);

    // 2 px border.
    for (int x = 0; x < RLCD_WIDTH; x++) {
        panel.SetPixel(x, 0, false);
        panel.SetPixel(x, 1, false);
        panel.SetPixel(x, RLCD_HEIGHT - 1, false);
        panel.SetPixel(x, RLCD_HEIGHT - 2, false);
    }
    for (int y = 0; y < RLCD_HEIGHT; y++) {
        panel.SetPixel(0, y, false);
        panel.SetPixel(1, y, false);
        panel.SetPixel(RLCD_WIDTH - 1, y, false);
        panel.SetPixel(RLCD_WIDTH - 2, y, false);
    }

    // Top-left marker: 40x40 filled black square.
    for (int y = 8; y < 48; y++) {
        for (int x = 8; x < 48; x++) panel.SetPixel(x, y, false);
    }

    // Bottom-right marker: 40x40 filled black square.
    for (int y = RLCD_HEIGHT - 48; y < RLCD_HEIGHT - 8; y++) {
        for (int x = RLCD_WIDTH - 48; x < RLCD_WIDTH - 8; x++) panel.SetPixel(x, y, false);
    }

    // Checkered strip: 4 px cells alternating across the middle.
    for (int y = RLCD_HEIGHT / 2 - 8; y < RLCD_HEIGHT / 2 + 8; y++) {
        for (int x = 60; x < RLCD_WIDTH - 60; x++) {
            const bool on = (((x - 60) / 4) + ((y - (RLCD_HEIGHT / 2 - 8)) / 4)) % 2 == 0;
            panel.SetPixel(x, y, on);
        }
    }

    // Centre cross, to expose any half-cell offset in the packing.
    for (int x = RLCD_WIDTH / 2 - 20; x < RLCD_WIDTH / 2 + 20; x++) panel.SetPixel(x, 30, false);
    for (int y = 8; y < 52; y++) panel.SetPixel(RLCD_WIDTH / 2, y, false);

    panel.PushFrame();
    ESP_LOGI(TAG, "read: TL square / BR square / border / checker strip / top cross");
}
