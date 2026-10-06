#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "lvgl.h"

// ---------------------------------------------------------------------------
// Waveshare ESP32-S3-RLCD-4.2  (ST7305 reflective monochrome LCD)
// Panel native geometry is 400 (x) x 300 (y) in landscape orientation.
// ---------------------------------------------------------------------------
#define RLCD_PIN_MOSI GPIO_NUM_12
#define RLCD_PIN_SCK  GPIO_NUM_11
#define RLCD_PIN_DC   GPIO_NUM_5
#define RLCD_PIN_CS   GPIO_NUM_40
#define RLCD_PIN_RST  GPIO_NUM_41
#define RLCD_PIN_TE   GPIO_NUM_6

#define RLCD_WIDTH   400
#define RLCD_HEIGHT  300

// Battery sense: 18650 holder -> 3x resistor divider -> GPIO4
#define BAT_ADC_PIN  GPIO_NUM_4
#define BAT_DIVIDER  3.0f

// Front-panel buttons (active low)
#define BTN_BOOT_PIN GPIO_NUM_0
#define BTN_KEY_PIN  GPIO_NUM_18

// Panel SPI clock. The ST7305 has no timing demands; 10 MHz keeps the
// 15 KB full-frame transfer at ~12 ms with plenty of margin.
#define RLCD_SPI_HZ  (10 * 1000 * 1000)

class RlcdPanel {
public:
    RlcdPanel();
    ~RlcdPanel();

    // Bring up SPI, panel IO, the ST7305 controller and LVGL.
    // Must be called once, before any LVGL object is created.
    esp_err_t Init();

    // Convert an RGB565 LVGL buffer area into the 1bpp panel frame buffer
    // and push the whole frame to the panel. Signature matches lv_display
    // flush callback requirements.
    void FlushRgb565(const lv_area_t *area, const uint8_t *color_p);

    // LVGL display object (400x300, RGB565 input, 1bpp output).
    lv_display_t *display() const { return display_; }

    // Fill the whole panel frame buffer with one colour and push it.
    void FillScreen(bool white);

    // Access to the raw 1bpp frame buffer (row-major, 400*300/8 = 15000 bytes)
    // kept for bring-up diagnostics.
    uint8_t *frame_buffer() { return frame_; }
    size_t   frame_bytes()  const { return frame_len_; }

    // Raw ST7305 primitives, used by the bring-up self test only.
    void WriteCommand(uint8_t cmd);
    void WriteData(uint8_t data);
    void PushFrame();

    // Direct 1bpp frame-buffer writes (used by the bring-up self test).
    void SetPixel(uint16_t x, uint16_t y, bool white);

private:
    esp_lcd_panel_io_handle_t io_ = nullptr;
    lv_display_t *display_       = nullptr;
    uint8_t *frame_              = nullptr;  // 1bpp panel buffer (PSRAM)
    uint8_t *lvgl_buf_           = nullptr;  // RGB565 LVGL draw buffer (PSRAM)
    size_t   frame_len_          = 0;
    size_t   lvgl_buf_len_       = 0;

    void ResetPanel();
    void ControllerInit();
};
