#include "battery.h"
#include "board_rlcd.h"

#include <esp_log.h>
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

battery_reading_t battery_read()
{
    battery_reading_t out = {};

    if (!s_ready) battery_init();
    if (!s_unit) return out;

    // Average a handful of samples; the ADC on the S3 is noisy around a
    // resistor divider.
    int64_t acc = 0;
    int     n = 0;
    for (int i = 0; i < 16; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_unit, ADC_CHANNEL_3, &raw) == ESP_OK) {
            acc += raw;
            n++;
        }
    }
    if (n == 0) return out;
    const int raw_avg = (int)(acc / n);

    int pin_mv = 0;
    if (s_cali_ok) {
        if (adc_cali_raw_to_voltage(s_cali, raw_avg, &pin_mv) != ESP_OK) return out;
    } else {
        // Rough fallback: 12 dB attenuation spans about 3100 mV.
        pin_mv = (int)((int64_t)raw_avg * 3100 / 4095);
    }

    out.valid = true;
    out.raw = raw_avg;
    out.pin_mv = pin_mv;
    out.volts = (float)pin_mv / 1000.0f * BAT_DIVIDER;

    // 18650 discharge curve endpoints as used by the vendor examples.
    //
    // This is a straight line, which is the crudest possible model: a Li-ion cell
    // holds most of its charge between 3.5 V and 3.9 V, so a linear map loses
    // resolution exactly where it matters and reads high through the middle. It is
    // only a placeholder until the real curve is measured - see the calibration
    // notes in the README.
    float pct = (out.volts - 2.5f) / (4.2f - 2.5f) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    out.percent = (int)(pct + 0.5f);

    return out;
}
