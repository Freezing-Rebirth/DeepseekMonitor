// Power policy: DFS plus automatic light sleep.
//
// Kept out of main.cpp because the reasoning below is long, it is the only place the
// application influences the power state, and it is the first thing to audit when
// consumption is questioned.

#include "power.h"

#include <esp_log.h>
#include <esp_pm.h>
#include <esp_private/esp_clk.h>

static const char *TAG = "power";

void power_configure()
{
    // CONFIG_PM_DFS_INIT_AUTO alone only installs dynamic frequency scaling: the
    // startup code calls esp_pm_configure() with the two frequencies and leaves
    // light_sleep_enable at false (esp_pm/pm_impl.c, esp_pm_impl_init). Automatic
    // light sleep has to be asked for, and without this call the system stops at the
    // DFS floor and never enters a low-power state at all - CONFIG_PM_ENABLE and
    // CONFIG_FREERTOS_USE_TICKLESS_IDLE are necessary but not sufficient.
    //
    // The ceiling is the configured CPU frequency (80 MHz); the floor is the XTAL,
    // 40 MHz, and that is what the CPU idles at. With WiFi modem sleep already on,
    // light sleep is what the system falls into between DTIM beacons.
    //
    // Light sleep is held off while a USB host is attached, and on this chip that is
    // not a nicety: the ESP32-S3's USB-Serial/JTAG peripheral cannot work during
    // light sleep or after waking from it. Measured here: with the lock absent, the
    // console port stops opening the moment this call takes effect, and the board has
    // to be put into download mode by hand before it can be flashed again. The lock
    // comes from CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION in sdkconfig.defaults. On battery
    // no host is present, so it is not taken and light sleep runs.
    //
    // A failure is logged rather than fatal: DFS still stands, and a reboot loop on a
    // device with no console attached is worse than losing the feature quietly.
    const esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = esp_clk_xtal_freq() / 1000000,
        .light_sleep_enable = true,
    };
    const esp_err_t err = esp_pm_configure(&pm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power management config failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "power management: %d..%d MHz, automatic light sleep on",
             (int)pm.max_freq_mhz, (int)pm.min_freq_mhz);
}
