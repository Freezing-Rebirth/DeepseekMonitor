#include "board_rlcd.h"

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>

static const char *TAG = "rlcd";

// ---------------------------------------------------------------------------
// ST7305 pixel blocking for the Waveshare 400x300 landscape panel.
//
//   8 pixels are packed per byte as 2 columns x 4 rows:
//
//     bit 7: (row 0, col 0)   bit 6: (row 0, col 1)
//     bit 5: (row 1, col 0)   bit 4: (row 1, col 1)
//     bit 3: (row 2, col 0)   bit 2: (row 2, col 1)
//     bit 1: (row 3, col 0)   bit 0: (row 3, col 1)
//
//   Address layout: index = byte_x * (height / 4) + block_y,
//   with the vertical axis inverted (y = 0 is the physical bottom of the
//   controller RAM).
//
//   400 x 300 => byte_x in [0,199], block_y in [0,74], stride 75,
//   frame length 199*75 + 74 + 1 = 15000 bytes.
// ---------------------------------------------------------------------------
#define RLCD_STRIDE_BLOCKS (RLCD_HEIGHT / 4)          // 75
#define RLCD_BYTES_PER_ROW (RLCD_WIDTH / 2)           // 200

RlcdPanel::RlcdPanel() = default;

RlcdPanel::~RlcdPanel()
{
    if (frame_)    { heap_caps_free(frame_);    frame_ = nullptr; }
    if (lvgl_buf_) { heap_caps_free(lvgl_buf_); lvgl_buf_ = nullptr; }
}

void RlcdPanel::WriteCommand(uint8_t cmd)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_, cmd, nullptr, 0));
}

void RlcdPanel::WriteData(uint8_t data)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_, -1, &data, 1));
}

void RlcdPanel::PushFrame()
{
    // Column address set (window used by the vendor reference driver)
    WriteCommand(0x2A);
    WriteData(0x12);
    WriteData(0x2A);

    // Page address set
    WriteCommand(0x2B);
    WriteData(0x00);
    WriteData(0xC7);

    // Memory write
    WriteCommand(0x2C);
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_, -1, frame_, frame_len_));
}

void RlcdPanel::ResetPanel()
{
    gpio_set_level(RLCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RLCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(RLCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void RlcdPanel::ControllerInit()
{
    ResetPanel();

    WriteCommand(0xD6); WriteData(0x17); WriteData(0x02);   // NVM load control
    WriteCommand(0xD1); WriteData(0x01);                    // Booster enable
    WriteCommand(0xC0); WriteData(0x11); WriteData(0x04);   // Gate voltage
    WriteCommand(0xC1); WriteData(0x69); WriteData(0x69); WriteData(0x69); WriteData(0x69);
    WriteCommand(0xC2); WriteData(0x19); WriteData(0x19); WriteData(0x19); WriteData(0x19);
    WriteCommand(0xC4); WriteData(0x4B); WriteData(0x4B); WriteData(0x4B); WriteData(0x4B);
    WriteCommand(0xC5); WriteData(0x19); WriteData(0x19); WriteData(0x19); WriteData(0x19);
    WriteCommand(0xD8); WriteData(0x80); WriteData(0xE9);
    WriteCommand(0xB2); WriteData(0x02);
    WriteCommand(0xB3);
    WriteData(0xE5); WriteData(0xF6); WriteData(0x05); WriteData(0x46);
    WriteData(0x77); WriteData(0x77); WriteData(0x77); WriteData(0x77);
    WriteData(0x76); WriteData(0x45);
    WriteCommand(0xB4);
    WriteData(0x05); WriteData(0x46);
    WriteData(0x77); WriteData(0x77); WriteData(0x77); WriteData(0x77);
    WriteData(0x76); WriteData(0x45);
    WriteCommand(0x62); WriteData(0x32); WriteData(0x03); WriteData(0x1F);
    WriteCommand(0xB7); WriteData(0x13);
    WriteCommand(0xB0); WriteData(0x64);

    WriteCommand(0x11);                                     // Sleep out
    vTaskDelay(pdMS_TO_TICKS(200));

    WriteCommand(0xC9); WriteData(0x00);
    WriteCommand(0x36); WriteData(0x48);                    // Memory access control
    WriteCommand(0x3A); WriteData(0x11);                    // Data format: 1bpp
    WriteCommand(0xB9); WriteData(0x20);
    WriteCommand(0xB8); WriteData(0x29);
    WriteCommand(0x21);                                     // Display inversion on
    WriteCommand(0x2A); WriteData(0x12); WriteData(0x2A);   // Column address
    WriteCommand(0x2B); WriteData(0x00); WriteData(0xC7);   // Page address
    WriteCommand(0x35); WriteData(0x00);                    // TE
    WriteCommand(0xD0); WriteData(0xFF);
    WriteCommand(0x38);                                     // High power mode
    WriteCommand(0x29);                                     // Display on
}

esp_err_t RlcdPanel::Init()
{
    // ---- frame buffers (PSRAM) ----
    frame_len_ = (size_t)RLCD_BYTES_PER_ROW * RLCD_STRIDE_BLOCKS;
    frame_ = (uint8_t *)heap_caps_malloc(frame_len_, MALLOC_CAP_SPIRAM);
    if (!frame_) {
        ESP_LOGE(TAG, "frame buffer alloc failed (%u bytes)", (unsigned)frame_len_);
        return ESP_ERR_NO_MEM;
    }
    memset(frame_, 0xFF, frame_len_);   // 1 = white

    // Full-screen single buffer so every flush covers the whole panel and the
    // RGB565 staging buffer is only ever needed for one flush at a time.
    // (A full 400x300 RGB565 buffer would cost 240 KB of PSRAM.)
    const size_t lvgl_buf_px = 64 * 32;
    lvgl_buf_len_ = lvgl_buf_px * 2;    // RGB565
    lvgl_buf_ = (uint8_t *)heap_caps_malloc(lvgl_buf_len_, MALLOC_CAP_SPIRAM);
    if (!lvgl_buf_) {
        ESP_LOGE(TAG, "lvgl buffer alloc failed (%u bytes)", (unsigned)lvgl_buf_len_);
        return ESP_ERR_NO_MEM;
    }

    // ---- SPI bus ----
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num     = RLCD_PIN_MOSI;
    buscfg.miso_io_num     = -1;
    buscfg.sclk_io_num     = RLCD_PIN_SCK;
    buscfg.quadwp_io_num   = -1;
    buscfg.quadhd_io_num   = -1;
    // A single colour push is one full panel frame (15000 bytes), so the bus
    // must allow transfers at least that large.
    buscfg.max_transfer_sz = (int)frame_len_;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // ---- panel IO ----
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num       = RLCD_PIN_DC;
    io_config.cs_gpio_num       = RLCD_PIN_CS;
    io_config.pclk_hz           = RLCD_SPI_HZ;
    io_config.lcd_cmd_bits      = 8;
    io_config.lcd_param_bits    = 8;
    io_config.spi_mode          = 0;
    io_config.trans_queue_depth = 4;
    io_config.flags.psram_dma_direct = 1;   // frame buffer lives in PSRAM
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST,
                                             &io_config, &io_));

    // ---- reset pin ----
    gpio_config_t rst = {};
    rst.intr_type    = GPIO_INTR_DISABLE;
    rst.mode         = GPIO_MODE_OUTPUT;
    rst.pin_bit_mask = (1ULL << RLCD_PIN_RST);
    rst.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&rst));

    ControllerInit();
    PushFrame();

    // ---- LVGL ----
    lv_init();

    display_ = lv_display_create(RLCD_WIDTH, RLCD_HEIGHT);
    if (!display_) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return ESP_FAIL;
    }
    lv_display_set_flush_cb(display_, [](lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
        auto *self = static_cast<RlcdPanel *>(lv_display_get_user_data(disp));
        self->FlushRgb565(area, px_map);
    });
    lv_display_set_user_data(display_, this);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    // 64x32 pixel tiles: small enough to keep the staging buffer cheap, large
    // enough that a full repaint needs only ~60 tiles.
    lv_display_set_buffers(display_, lvgl_buf_, nullptr, lvgl_buf_len_,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "ST7305 ready: %dx%d, frame %u bytes, lvgl buffer %u bytes",
             RLCD_WIDTH, RLCD_HEIGHT, (unsigned)frame_len_, (unsigned)lvgl_buf_len_);
    return ESP_OK;
}

void RlcdPanel::SetPixel(uint16_t x, uint16_t y, bool white)
{
    if (x >= RLCD_WIDTH || y >= RLCD_HEIGHT) return;

    const uint16_t inv_y   = (uint16_t)(RLCD_HEIGHT - 1 - y);
    const uint16_t byte_x  = x >> 1;
    const uint16_t block_y = inv_y >> 2;
    const uint32_t index   = (uint32_t)byte_x * RLCD_STRIDE_BLOCKS + block_y;
    const uint8_t  bit     = (uint8_t)(7 - (((inv_y & 3) << 1) | (x & 1)));
    const uint8_t  mask    = (uint8_t)(1u << bit);

    if (white) frame_[index] |=  mask;
    else       frame_[index] &= ~mask;
}

// ---------------------------------------------------------------------------
// Pixel bit mask lookup.
//
// For the 400x300 landscape panel the bit index is
//     7 - (((inv_y & 3) << 1) | (x & 1))
// which depends only on the low bits of x and y, so a 4x8 table removes the
// shift-or-subtract from the inner flush loop.
// ---------------------------------------------------------------------------
static uint8_t s_bit_mask[4][8] = {};
static bool    s_bit_mask_ready = false;

static void build_bit_mask_table()
{
    for (int x = 0; x < 4; x++) {
        for (int y = 0; y < 8; y++) {
            s_bit_mask[x][y] = (uint8_t)(1u << (7 - (((y & 3) << 1) | (x & 1))));
        }
    }
    s_bit_mask_ready = true;
}

void RlcdPanel::FlushRgb565(const lv_area_t *area, const uint8_t *color_p)
{
    if (!s_bit_mask_ready) build_bit_mask_table();

    const uint16_t *src = reinterpret_cast<const uint16_t *>(color_p);

    // Render mode is PARTIAL, so this runs once per dirty tile rather than once
    // per frame. The whole panel frame is pushed at the end of every tile, which
    // is cheap at 10 MHz (15 KB, ~12 ms) and keeps the write path simple.
    for (int32_t y = area->y1; y <= area->y2; y++) {
        const uint32_t inv_y   = (uint32_t)(RLCD_HEIGHT - 1 - y);
        const uint32_t block_y = inv_y >> 2;
        const uint32_t ybits   = inv_y & 7;

        for (int32_t x = area->x1; x <= area->x2; x++) {
            const uint32_t index = ((uint32_t)x >> 1) * RLCD_STRIDE_BLOCKS + block_y;
            const uint8_t  mask  = s_bit_mask[(uint32_t)x & 3][ybits];

            if (*src >= 0x7fff) frame_[index] |=  mask;
            else                frame_[index] &= (uint8_t)~mask;
            src++;
        }
    }

    PushFrame();
    lv_display_flush_ready(display_);
}

void RlcdPanel::FillScreen(bool white)
{
    memset(frame_, white ? 0xFF : 0x00, frame_len_);
    PushFrame();
}
