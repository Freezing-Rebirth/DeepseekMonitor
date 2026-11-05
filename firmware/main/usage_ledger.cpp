#include "usage_ledger.h"
#include "ledger_math.h"
#include "store.h"
#include "debug_config.h"

#include <string.h>
#include <esp_log.h>

static const char *TAG = "ledger";

// ---------------------------------------------------------------------------
// Flash wear. Writing five integer keys every three minutes looks alarming, so the
// budget is worked out here rather than guessed at.
//
//   IDF file-system-considerations.rst: NOR flash survives ~100,000 erase
//       cycles per sector.
//   IDF nvs_flash.rst: for data types fitting one entry, NVS "reduces the
//       frequency of flash erase to flash write operations ... by a factor of 126"
//       - i.e. 126 entries per 4 KB page.
//   partitions.csv: the nvs partition is 0x6000 = 24 KB = 6 pages.
//
//   5 keys per poll -> 126 / 5 = 25 polls to fill a page
//   6 pages         -> 150 polls before a page must be erased
//   480 polls/day   -> 3.2 erases/day -> ~1168 erases/year
//   100,000 / 1168  -> ~86 years
//
// So no RAM buffering or batching is needed at this cadence: the counter is the
// bottleneck's opposite. That changes if the poll interval drops to seconds - at
// one poll per 10 s it would be ~20 days - and then the answer is not batching but
// a different medium (FatFS over the wear-levelling library, or PSRAM with a
// persist-on-shutdown hook).
// ---------------------------------------------------------------------------

// The totals live in ledger_math.cpp's plain struct; this file owns their storage.
static ledger_totals_t s_totals = {};

// Last timestamp the stats diagnostics were printed for.
static int64_t s_last_diag_ts = 0;

static bool s_loaded = false;

static void ledger_load()
{
    s_totals = (ledger_totals_t){};
    store_ledger_load(&s_totals);
    s_loaded = true;
    ESP_LOGI(TAG, "loaded totals: count=%u spent=%.4f CNY over %us (last ts %u)",
             (unsigned)s_totals.count, (double)ledger_cf_to_cny(s_totals.spent_cf),
             (unsigned)s_totals.elapsed_sec, (unsigned)s_totals.last_ts);
}

static void ledger_save()
{
    store_ledger_save(&s_totals);
}

void ledger_init()
{
    if (!s_loaded) ledger_load();
}

void ledger_record(float total_balance_cny, int64_t unix_ts)
{
    if (!s_loaded) ledger_load();

    const uint32_t now_cf = ledger_cny_to_cf(total_balance_cny);
    bool    first = false;
    int64_t dt    = 0;
    ledger_math_record(&s_totals, now_cf, unix_ts, &first, &dt);

    // An unusable timestamp changes nothing, so there is nothing to write.
    if (!first && dt == 0) return;

    ledger_save();

    if (first) {
        ESP_LOGI(TAG, "ledger started at %.4f CNY", (double)total_balance_cny);
    } else {
        ESP_LOGI(TAG, "recorded %.4f CNY (total spent %.4f over %us, dt=%llds)",
                 (double)total_balance_cny, (double)ledger_cf_to_cny(s_totals.spent_cf),
                 (unsigned)s_totals.elapsed_sec, (long long)dt);
    }
}

ledger_stats_t ledger_stats(float current_balance_cny, int64_t unix_ts)
{
    if (!s_loaded) ledger_load();

    // One line per new reading, gated like the rest of the periodic diagnostics in
    // main.cpp: it is what tells "not enough history" (window too small) apart from
    // "no spend at all" (spent is zero), but it is noise once the panel is working.
    if (unix_ts != s_last_diag_ts) {
        s_last_diag_ts = unix_ts;
#if DEBUG_LOGS
        ESP_LOGI(TAG, "stats: count=%u window=%llds spent=%.4f current=%.2f",
                 (unsigned)s_totals.count, (long long)s_totals.elapsed_sec,
                 (double)ledger_cf_to_cny(s_totals.spent_cf), (double)current_balance_cny);
#endif
    }

    return ledger_math_stats(&s_totals, current_balance_cny);
}

int ledger_sample_count()
{
    if (!s_loaded) ledger_load();
    return (int)s_totals.count;
}

int64_t ledger_window_sec()
{
    if (!s_loaded) ledger_load();
    return (int64_t)s_totals.elapsed_sec;
}
