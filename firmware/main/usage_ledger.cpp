#include "usage_ledger.h"

#include <string.h>
#include <math.h>
#include <esp_log.h>
#include <nvs.h>

static const char *TAG = "ledger";

static const char *LEDGER_NVS_NS = "dsledger";

// Keys. "count" / "spent" / "elapsed" / "ref" are integers; NVS stores each as a
// single entry, which wear-levelling handles far better than the blob this used
// to write. The old "samples" blob is left untouched so a device upgrading in
// place does not fail to open the namespace; it is simply no longer read.
static const char *KEY_COUNT   = "count";
static const char *KEY_SPENT   = "spent";     // 0.01 fen units
static const char *KEY_ELAPSED = "elapsed";   // seconds
static const char *KEY_REF     = "ref";       // 0.01 fen units
static const char *KEY_TS      = "lastts";    // unix seconds of the last reading

// Longest single interval that may be credited to the totals.
//
// A reading is taken every five minutes, so anything much longer means the device
// was off or offline. The cap exists so an outage cannot dump unmonitored time into
// the average and drag BURN down - but it must not be so tight that a genuine
// outage distorts things the other way. Spend and time are credited separately
// (spend from the balance delta, time from this interval), so capping the interval
// at two hours while still crediting three days of real spend inflated BURN by
// about 36x. At 24 hours the two stay in proportion for any plausible outage, and
// only a multi-day gap is trimmed.
#define LEDGER_MAX_INTERVAL_SEC (24 * 3600)

// ---------------------------------------------------------------------------
// Flash wear. Writing five integer keys every five minutes looks alarming, so the
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
//   288 polls/day   -> 1.92 erases/day -> ~701 erases/year
//   100,000 / 701   -> ~143 years
//
// So no RAM buffering or batching is needed at this cadence: the counter is the
// bottleneck's opposite. That changes if the poll interval drops to seconds - at
// one poll per 10 s it would be ~20 days - and then the answer is not batching but
// a different medium (FatFS over the wear-levelling library, or PSRAM with a
// persist-on-shutdown hook).
// ---------------------------------------------------------------------------

// Totals. spent_cf counts *down* in hundredths of a fen: 0.01 fen = 1e-4 CNY, so
// a full fen is 100 units and the counter holds up to ~42949 CNY of spend in a
// uint32 before wrapping - far beyond what this device will ever see.
static uint32_t s_count    = 0;
static uint32_t s_spent_cf = 0;
static uint32_t s_elapsed  = 0;
static uint32_t s_ref_cf   = 0;   // balance the next delta is measured against

// Timestamp of the previous reading in unix seconds. Persisted: without it every
// reboot reset the interval to zero, so a device that restarted periodically
// accumulated almost no monitored time and BURN never became valid.
static uint32_t s_last_ts = 0;   // fits unix seconds until 2106

// Last timestamp the stats diagnostics were printed for.
static int64_t s_last_diag_ts = 0;

static bool s_loaded = false;

static inline uint32_t cny_to_cf(float cny)
{
    if (cny <= 0.0f) return 0u;
    const double cf = (double)cny * 10000.0;   // 1 CNY = 100 fen = 10000 cf
    if (cf >= 4294967295.0) return 4294967295u;
    return (uint32_t)(cf + 0.5);
}

static inline float cf_to_cny(uint32_t cf)
{
    return (float)((double)cf / 10000.0);
}

static void load_u32(nvs_handle_t h, const char *key, uint32_t *out)
{
    uint32_t v = 0;
    if (nvs_get_u32(h, key, &v) == ESP_OK) {
        *out = v;
    }
}

static void ledger_load()
{
    memset(&s_count, 0, sizeof(s_count));
    nvs_handle_t h;
    if (nvs_open(LEDGER_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        load_u32(h, KEY_COUNT,   &s_count);
        load_u32(h, KEY_SPENT,   &s_spent_cf);
        load_u32(h, KEY_ELAPSED, &s_elapsed);
        load_u32(h, KEY_REF,     &s_ref_cf);
        load_u32(h, KEY_TS,      &s_last_ts);
        nvs_close(h);
    }
    s_loaded = true;
    ESP_LOGI(TAG, "loaded totals: count=%u spent=%.4f CNY over %us (last ts %u)",
             (unsigned)s_count, (double)cf_to_cny(s_spent_cf), (unsigned)s_elapsed,
             (unsigned)s_last_ts);
}

static void ledger_save()
{
    nvs_handle_t h;
    if (nvs_open(LEDGER_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, KEY_COUNT,   s_count);
    nvs_set_u32(h, KEY_SPENT,   s_spent_cf);
    nvs_set_u32(h, KEY_ELAPSED, s_elapsed);
    nvs_set_u32(h, KEY_REF,     s_ref_cf);
    nvs_set_u32(h, KEY_TS,      s_last_ts);
    nvs_commit(h);
    nvs_close(h);
}

void ledger_init()
{
    if (!s_loaded) ledger_load();
}

void ledger_record(float total_balance_cny, int64_t unix_ts)
{
    if (!s_loaded) ledger_load();
    if (unix_ts <= 0) return;

    const uint32_t now_cf = cny_to_cf(total_balance_cny);

    if (s_count == 0) {
        // First ever reading: nothing to compare against yet.
        s_ref_cf  = now_cf;
        s_count   = 1;
        s_last_ts = (uint32_t)unix_ts;
        ledger_save();
        ESP_LOGI(TAG, "ledger started at %.4f CNY", (double)total_balance_cny);
        return;
    }

    // Credit the interval since the previous reading, taken from wall-clock
    // timestamps so the time survives a reboot. A span longer than
    // LEDGER_MAX_INTERVAL_SEC means the device was off or offline; only that cap is
    // credited, so unmonitored time cannot dilute the average.
    int64_t dt = 0;
    if (s_last_ts > 0 && unix_ts > (int64_t)s_last_ts) {
        dt = unix_ts - (int64_t)s_last_ts;
    }
    if (dt > LEDGER_MAX_INTERVAL_SEC) dt = LEDGER_MAX_INTERVAL_SEC;
    if (dt > 0) s_elapsed += (uint32_t)dt;

    if (now_cf < s_ref_cf) {
        s_spent_cf += (s_ref_cf - now_cf);
    }
    // A higher balance means a top-up (or a corrected reading): the reference
    // simply moves up and no spend is recorded for the interval.
    s_ref_cf  = now_cf;
    s_count++;
    s_last_ts = (uint32_t)unix_ts;

    ledger_save();
    ESP_LOGI(TAG, "recorded %.4f CNY (total spent %.4f over %us, dt=%llds)",
             (double)total_balance_cny, (double)cf_to_cny(s_spent_cf),
             (unsigned)s_elapsed, (long long)dt);
}

ledger_stats_t ledger_stats(float current_balance_cny, int64_t unix_ts)
{
    ledger_stats_t out = {};
    if (!s_loaded) ledger_load();
    if (s_count < 2) return out;

    const double spent  = (double)cf_to_cny(s_spent_cf);
    const int64_t window = (int64_t)s_elapsed;

    // One line per new reading, so "not enough history" (window too small) can be
    // told apart from "no spend at all" (spent is zero).
    if (unix_ts != s_last_diag_ts) {
        s_last_diag_ts = unix_ts;
        ESP_LOGI(TAG, "stats: count=%u window=%llds spent=%.4f current=%.2f",
                 (unsigned)s_count, (long long)window, spent,
                 (double)current_balance_cny);
    }

    if (window < LEDGER_MIN_WINDOW_SEC || spent <= 0.0) {
        return out;
    }

    out.valid = true;
    // The window is everything since monitoring began, so this is the average rate
    // over the whole monitored period rather than over a short recent slice.
    out.daily_burn_cny = (float)(spent * 86400.0 / (double)window);
    out.runway_days = (out.daily_burn_cny > 0.0001f)
                          ? (current_balance_cny / out.daily_burn_cny)
                          : 0.0f;
    return out;
}

int ledger_sample_count()
{
    if (!s_loaded) ledger_load();
    return (int)s_count;
}

int64_t ledger_window_sec()
{
    if (!s_loaded) ledger_load();
    return (int64_t)s_elapsed;
}
