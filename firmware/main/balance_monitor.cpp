// One HTTPS balance query, and the state it leaves behind for the panel.
//
// Split out of main.cpp because the query is the only blocking network operation the
// application performs on its own behalf, and because the balance, the ledger and the
// derived burn rate are one fact with three representations - keeping them in one file
// is what stops the panel, the log and the API from disagreeing.

#include "balance_monitor.h"

#include <esp_log.h>

#include "app_config.h"
#include "app_config_store.h"
#include "clock_time.h"
#include "deepseek_api.h"
#include "usage_ledger.h"

static const char *TAG = "balance";

static balance_snapshot_t s_snapshot = {};

void balance_poll()
{
    char key[160];
    if (!app_config_api_key(key, sizeof(key))) {
        return;
    }

    ESP_LOGI(TAG, "querying DeepSeek balance");
    const ds_balance_t bal = ds_query_balance(key, 15000);

    if (!bal.ok) {
        ESP_LOGW(TAG, "balance query failed: %s (http %d)", bal.error, bal.http_status);
        return;
    }

    s_snapshot.valid = true;
    s_snapshot.cny = bal.total_balance;

    ESP_LOGI(TAG, "balance %.2f %s (granted %.2f, topped up %.2f, available=%d)",
             (double)bal.total_balance, bal.currency,
             (double)bal.granted_balance, (double)bal.topped_up_balance,
             (int)bal.is_available);

    // The ledger only makes sense against a real clock: a sample stamped with an
    // unsynced time would land years away from the previous one.
    if (!clock_is_synced()) {
        return;
    }

    const int64_t now = (int64_t)clock_now();
    ledger_record(bal.total_balance, now);
    const ledger_stats_t st = ledger_stats(bal.total_balance, now);
    s_snapshot.ledger_valid = st.valid;
    s_snapshot.daily_burn_cny = st.daily_burn_cny;
    s_snapshot.runway_days = st.runway_days;
    ESP_LOGI(TAG, "ledger: %d samples over %lld s, burn %.2f/Day, runway %.1f days",
             ledger_sample_count(), (long long)ledger_window_sec(),
             (double)st.daily_burn_cny, (double)st.runway_days);
}

balance_snapshot_t balance_snapshot()
{
    return s_snapshot;
}
