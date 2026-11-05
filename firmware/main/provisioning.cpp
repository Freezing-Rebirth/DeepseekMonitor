// The setup portal's policy: when it is raised, what happens when credentials arrive,
// and how a bad password recovers without a reboot.
//
// The task has to exist because the form is a web server and the main loop must keep
// rendering while the user types. Everything else here is the state machine around it.

#include "provisioning.h"

#include <esp_log.h>
#include <esp_wifi.h>

#include "balance_monitor.h"
#include "app_config_store.h"
#include "clock_time.h"
#include "wifi_portal.h"
#include "wifi_prov.h"

static const char *TAG = "provision";

// 12 KB of stack: this task goes on to call balance_poll(), and the TLS handshake plus
// certificate-bundle parse does not fit in 6 KB - at 6 KB it tripped the FreeRTOS
// stack-overflow hook and rebooted the board in a loop.
#define PORTAL_TASK_STACK 12288

// How long to stay offline before offering the setup form again. Long enough that a
// router reboot or a brief dropout does not raise the access point unnecessarily.
#define PORTAL_AUTO_OPEN_MS (2 * 60 * 1000)

static volatile bool s_active = false;

// When the station first went down with credentials stored. Drives the automatic
// re-opening of the setup form, so a bad password recovers without a reboot.
static TickType_t s_offline_since = 0;

static void portal_task(void *);

// Run the portal to completion and return true when credentials came back.
// Must be called with s_active already set and no portal task running.
static bool portal_run(bool wait_forever)
{
    wifi_prov_portal_clear_submitted();
    if (!wifi_prov_portal_start()) {
        s_active = false;
        return false;
    }

    if (wait_forever) {
        ESP_LOGI(TAG, "portal waiting indefinitely for the setup form");
        while (!wifi_prov_portal_submitted()) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    } else {
        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5 * 60 * 1000);
        while (!wifi_prov_portal_submitted() && xTaskGetTickCount() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    const bool submitted = wifi_prov_portal_submitted();
    wifi_prov_portal_stop();
    s_active = false;
    return submitted;
}

static void portal_task(void *)
{
    // Always wait indefinitely. The form is the only way to fix a bad WiFi password or
    // a moved router, and a timed window meant that a second attempt could arrive after
    // the server had already shut down - the page then loaded into nothing. The portal
    // is cheap to hold open: the station is suspended while it runs, so it is not
    // fighting the access point.
    if (!portal_run(true)) {
        s_active = false;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "credentials received, rejoining network");
    // The driver is still holding the previous station config; the form wrote to NVS
    // only. Apply the new one before reconnecting, or this attempt targets the old
    // network and fails until the next reboot.
    wifi_apply_stored_credentials();
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_wifi_connect();
    if (wifi_wait_connected(25000)) {
        ESP_LOGI(TAG, "reconnected");
        if (clock_sync_sntp(15000)) ESP_LOGI(TAG, "clock synchronised");
        balance_poll();
    } else {
        // Wrong password or unreachable network. The main loop notices that the station
        // is down and raises the form again, so the user gets another attempt without
        // touching the board.
        ESP_LOGW(TAG, "could not join the new network; the setup form will reopen");
    }

    vTaskDelete(nullptr);
}

void provisioning_begin()
{
    if (s_active) return;
    s_active = true;
    xTaskCreatePinnedToCore(portal_task, "portal", PORTAL_TASK_STACK, nullptr, 3, nullptr, 0);
}

bool provisioning_active()
{
    return s_active;
}

void provisioning_after_wifi(bool online)
{
    if (online) return;

    if (!appcfg_has_wifi()) {
        ESP_LOGI(TAG, "no stored network; starting provisioning portal");
    } else {
        // Has credentials that will not connect: a wrong password, a moved router, or a
        // network that is simply down. Either way the user cannot fix it from the panel,
        // and the only recovery used to be a BOOT long press they would have to know
        // about. Raise the form immediately, in the background, so the UI still comes up
        // and the user can retry as often as they like.
        ESP_LOGW(TAG, "stored network did not connect; opening the setup portal");
    }
    provisioning_begin();
}

void provisioning_tick()
{
    // Auto-recover an unreachable network.
    //
    // If the board cannot reach the stored network there is nothing the user can do
    // from the panel, so the setup form is raised on its own. Without this, a wrong
    // WiFi password looks exactly like dead hardware: the board associates, fails the
    // handshake, retries forever, and never offers a way in. The delay keeps a brief
    // blip - a router reboot, say - from raising the access point when the link is
    // about to come back by itself.
    if (!s_active && appcfg_has_wifi() && !wifi_get_status().connected) {
        if (s_offline_since == 0) {
            s_offline_since = xTaskGetTickCount();
        } else if ((xTaskGetTickCount() - s_offline_since) >= pdMS_TO_TICKS(PORTAL_AUTO_OPEN_MS)) {
            ESP_LOGW(TAG, "offline for %u s with stored credentials; opening the setup portal",
                     (unsigned)(PORTAL_AUTO_OPEN_MS / 1000));
            s_offline_since = 0;
            provisioning_begin();
        }
    } else if (wifi_get_status().connected) {
        s_offline_since = 0;
    }

    // Hold BOOT for ~3 s at any time to re-enter provisioning.
    if (!s_active && boot_button_long_pressed()) {
        ESP_LOGI(TAG, "BOOT long press: starting provisioning portal");
        provisioning_begin();
    }
}
