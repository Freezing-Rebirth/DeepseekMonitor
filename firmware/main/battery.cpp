#include "battery.h"
#include "board_rlcd.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_unit = nullptr;
static adc_cali_handle_t         s_cali = nullptr;
static bool                      s_cali_ok = false;
static bool                      s_ready = false;

void battery_init()
{
    if (s_ready) return;

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    if (adc_oneshot_new_unit(&unit_cfg, &s_unit) != ESP_OK) {
        ESP_LOGW(TAG, "ADC unit init failed");
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    // 12 dB attenuation raises the measurable ceiling to ~3.1 V, which the
    // divided cell voltage stays well under.
    chan_cfg.atten = ADC_ATTEN_DB_12;
    if (adc_oneshot_config_channel(s_unit, ADC_CHANNEL_3, &chan_cfg) != ESP_OK) {
        // GPIO4 is ADC1 channel 3 on the ESP32-S3.
        ESP_LOGW(TAG, "ADC channel config failed");
        return;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = ADC_UNIT_1;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "ADC calibration unavailable, using raw counts");
    }

    s_ready = true;
    ESP_LOGI(TAG, "battery sense ready (calibrated=%d)", (int)s_cali_ok);
}

// ---------------------------------------------------------------------------
// Voltage -> state of charge
//
// Two corrections over the linear map this replaced, which came from the vendor's
// ESPHome example (2.5 V = 0%, 4.2 V = 100%):
//
//   Empty is 3.0 V, not 2.5 V. The board cannot run below about 3.0 V: the ESP32-S3
//   needs its 3.3 V rail regulated, and the regulator needs roughly 100-200 mV of
//   headroom. The brownout detector is left at its lowest step (2.44 V), so it is
//   never what stops the board. With the old map the panel therefore read about 29%
//   at the moment the device died - a board that looks like it failed with a third
//   of its charge left.
//
//   A Li-ion cell does not discharge linearly. It sits on a plateau around
//   3.7-3.9 V for most of its capacity, so a straight line between two endpoints
//   moves far too slowly through the middle. At 3.549 V the old map read 62% where
//   the curve reads 19%, an error of 43 points.
//
// The curve is a resting-voltage discharge profile for a 18650 at 0.2 C, 25 C.
// Between points it interpolates, which is what the flat middle needs to stay
// monotonic without a large table.
// ---------------------------------------------------------------------------
typedef struct {
    uint16_t mv;
    uint8_t  pct;
} soc_point_t;

static const soc_point_t kSocCurve[] = {
    { 3000,   0 },
    { 3200,   4 },
    { 3350,   8 },
    { 3500,  15 },
    { 3620,  25 },
    { 3700,  35 },
    { 3750,  45 },
    { 3800,  55 },
    { 3850,  65 },
    { 3920,  75 },
    { 4000,  85 },
    { 4100,  95 },
    { 4200, 100 },
};
#define SOC_POINTS (sizeof(kSocCurve) / sizeof(kSocCurve[0]))

int battery_percent_from_volts(float volts)
{
    const int mv = (int)(volts * 1000.0f + 0.5f);

    if (mv <= kSocCurve[0].mv) return kSocCurve[0].pct;
    if (mv >= kSocCurve[SOC_POINTS - 1].mv) return kSocCurve[SOC_POINTS - 1].pct;

    for (size_t i = 1; i < SOC_POINTS; i++) {
        const soc_point_t *lo = &kSocCurve[i - 1];
        const soc_point_t *hi = &kSocCurve[i];
        if (mv <= hi->mv) {
            const int span = hi->mv - lo->mv;
            const int num  = (mv - lo->mv) * (hi->pct - lo->pct);
            return lo->pct + (num + span / 2) / span;
        }
    }
    return kSocCurve[SOC_POINTS - 1].pct;
}

battery_reading_t battery_read()
{
    battery_reading_t out = {};

    if (!s_ready) battery_init();
    if (!s_unit) {
        ESP_LOGW(TAG, "read attempted before the ADC was ready");
        return out;
    }

    // Two passes. A single failed conversion is normally transient - the radio
    // competing for the ADC is the usual cause - so an immediate second attempt
    // recovers it and the panel keeps its number instead of blanking to "BAT:--"
    // for a whole refresh interval.
    int64_t acc = 0;
    int     n = 0;
    int     last_err = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 16; i++) {
            int raw = 0;
            const esp_err_t err = adc_oneshot_read(s_unit, ADC_CHANNEL_3, &raw);
            if (err == ESP_OK) {
                acc += raw;
                n++;
            } else {
                last_err = (int)err;
            }
        }
        if (n > 0) break;
        // Nothing at all came back; yield briefly and try once more.
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (n == 0) {
        ESP_LOGW(TAG, "ADC produced no sample in 32 attempts (last err %d)", last_err);
        return out;
    }
    const int raw_avg = (int)(acc / n);

    int pin_mv = 0;
    if (s_cali_ok) {
        const esp_err_t err = adc_cali_raw_to_voltage(s_cali, raw_avg, &pin_mv);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "calibration rejected raw=%d (err %d)", raw_avg, (int)err);
            return out;
        }
    } else {
        // Rough fallback: 12 dB attenuation spans about 3100 mV.
        pin_mv = (int)((int64_t)raw_avg * 3100 / 4095);
    }

    out.valid = true;
    out.raw = raw_avg;
    out.pin_mv = pin_mv;
    out.volts = (float)pin_mv / 1000.0f * BAT_DIVIDER;
    out.percent = battery_percent_from_volts(out.volts);

    return out;
}
