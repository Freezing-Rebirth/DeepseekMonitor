#include "battery.h"
#include "board_rlcd.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_unit = nullptr;
static adc_cali_handle_t         s_cali = nullptr;
static bool                      s_cali_ok = false;
static bool                      s_ready = false;

// ---------------------------------------------------------------------------
// The persisted copy of the last reading that worked.
//
// A reading is taken at every repaint, but the ADC is not always ours: with light
// sleep enabled the conversions start failing with ESP_ERR_TIMEOUT from the
// session's first sleep onwards - not the radio, and not the timing, which is what
// it looked like until both configurations were measured side by side. Then every
// repaint would show "BAT:--" - including the first
// one five seconds after boot, before any reading has had a chance. Keeping the
// last good value in NVS means a reboot repaints with the charge the device last
// knew. See battery_restore() in battery.h.
//
// Two entries in their own namespace. The write happens only when the percentage
// steps, so the traffic is a few writes a day - the same order as the ledger's
// five keys every five minutes, whose flash budget is worked out in
// usage_ledger.cpp and comes to centuries.
// ---------------------------------------------------------------------------
static const char *BAT_NVS_NS  = "dsbat";
static const char *KEY_PERCENT = "pct";
static const char *KEY_MV      = "mv";     // cell millivolts, divider undone

// How many times the ADC unit has had to be re-created since boot.
//
// With light sleep enabled on this board the first sleep breaks the conversion path and
// every read then fails; reinit() below is the repair. A healthy board sitting at 1 after
// its first sleep has recovered and is reading again; a number that keeps climbing means
// the repair is not working and the panel is only ever repeating the last known charge.
// Without it the only signal is the log, which is exactly what a sleeping board cannot
// deliver - measured over JTAG, this counter is what separated the two outcomes.
static uint32_t s_recoveries = 0;

uint32_t battery_recoveries() { return s_recoveries; }

static void persist_if_changed(const battery_reading_t &r)
{
    if (!r.valid) return;

    nvs_handle_t h;
    if (nvs_open(BAT_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;

    uint8_t stored_pct = 0;
    const bool have = (nvs_get_u8(h, KEY_PERCENT, &stored_pct) == ESP_OK);
    if (!have || stored_pct != (uint8_t)r.percent) {
        // The cell voltage sits on a plateau for hours and the ADC wanders by a
        // count or two, so an unconditional write every minute would turn a
        // rounding wobble into flash traffic that carries no information.
        nvs_set_u8(h, KEY_PERCENT, (uint8_t)r.percent);
        nvs_set_u32(h, KEY_MV, (uint32_t)(r.volts * 1000.0f + 0.5f));
        nvs_commit(h);
        ESP_LOGI(TAG, "stored %d%% (%.3f V) for the next boot",
                 r.percent, (double)r.volts);
    }
    nvs_close(h);
}

bool battery_restore(battery_reading_t *out)
{
    if (!out) return false;
    *out = {};

    nvs_handle_t h;
    if (nvs_open(BAT_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;

    uint8_t  pct = 0;
    uint32_t mv  = 0;
    const bool ok = (nvs_get_u8(h, KEY_PERCENT, &pct) == ESP_OK) &&
                    (nvs_get_u32(h, KEY_MV, &mv) == ESP_OK);
    nvs_close(h);
    if (!ok) return false;

    out->valid   = true;
    out->percent = (int)pct;
    out->volts   = (float)mv / 1000.0f;
    return true;
}

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

// Tear the ADC down and build it again.
//
// battery_init() claims the unit, configures the channel and installs the curve-fitting
// calibration; running that a second time is what puts the conversion path back together
// after light sleep has broken it. The handles are released in the right order (calibration
// before unit) so nothing is leaked when this runs on every repaint.
static void reinit()
{
    if (s_cali) {
        adc_cali_delete_scheme_curve_fitting(s_cali);
        s_cali = nullptr;
    }
    if (s_unit) {
        adc_oneshot_del_unit(s_unit);
        s_unit = nullptr;
    }
    s_cali_ok = false;
    s_ready = false;
    battery_init();
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
        // Try to get the ADC back before giving up on it.
        //
        // On this chip light sleep is what takes it: from the session's first sleep
        // onwards every conversion returns ESP_ERR_TIMEOUT, while WiFi, the panel and
        // the ledger all carry on. Re-creating the unit re-runs the whole analogue
        // setup (channel, attenuation, hardware calibration), so a sleeping board can
        // still publish a number rather than only ever repeating the last known one.
        // Measured cost when it never fires: nothing - it runs only after 32 failures.
        ESP_LOGW(TAG, "ADC produced no sample in 32 attempts (last err %d); re-creating the unit",
                 last_err);
        s_recoveries++;
        reinit();

        acc = 0;
        n = 0;
        for (int i = 0; i < 16; i++) {
            int raw = 0;
            if (adc_oneshot_read(s_unit, ADC_CHANNEL_3, &raw) == ESP_OK) {
                acc += raw;
                n++;
            }
        }
        if (n == 0) {
            ESP_LOGW(TAG, "re-created ADC still produced no sample; keeping the last good reading");
            return out;
        }
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

    persist_if_changed(out);
    return out;
}
