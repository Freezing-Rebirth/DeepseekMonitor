// The NVS data namespaces. See store.h.
//
// One namespace per owner, one key table, all of it in this file. Nothing else in the
// application includes nvs.h.

#include "store.h"

#include <string.h>
#include <nvs.h>

// Namespaces. Short by NVS's 15-character limit; the prefixes say which module owns them.
static const char *NS_BATTERY  = "dsbat";
static const char *NS_LEDGER   = "dsledger";
static const char *NS_HOLIDAYS = "dshol";

// Battery keys.
static const char *KEY_BAT_PCT = "pct";
static const char *KEY_BAT_MV  = "mv";

// Ledger keys. "count" / "spent" / "elapsed" / "ref" are integers; NVS stores each as a
// single entry, which wear-levelling handles far better than a blob. The old "samples"
// blob in this namespace is left untouched so a device upgrading in place does not fail
// to open it; it is simply no longer read.
static const char *KEY_COUNT   = "count";
static const char *KEY_SPENT   = "spent";     // 0.01 fen units
static const char *KEY_ELAPSED = "elapsed";   // seconds
static const char *KEY_REF     = "ref";       // 0.01 fen units
static const char *KEY_TS      = "lastts";    // unix seconds of the last reading

static const char *KEY_HOL_BLOB = "cache";

bool store_battery_load(uint8_t *pct, uint32_t *mv)
{
    if (!pct || !mv) return false;

    nvs_handle_t h;
    if (nvs_open(NS_BATTERY, NVS_READONLY, &h) != ESP_OK) return false;

    const bool ok = (nvs_get_u8(h, KEY_BAT_PCT, pct) == ESP_OK) &&
                    (nvs_get_u32(h, KEY_BAT_MV, mv) == ESP_OK);
    nvs_close(h);
    return ok;
}

void store_battery_save(uint8_t pct, uint32_t mv)
{
    nvs_handle_t h;
    if (nvs_open(NS_BATTERY, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, KEY_BAT_PCT, pct);
    nvs_set_u32(h, KEY_BAT_MV, mv);
    nvs_commit(h);
    nvs_close(h);
}

bool store_ledger_load(ledger_totals_t *out)
{
    if (!out) return false;
    *out = (ledger_totals_t){};

    nvs_handle_t h;
    if (nvs_open(NS_LEDGER, NVS_READONLY, &h) != ESP_OK) return false;
    // Each key independently: a namespace written by an older firmware may be missing one.
    if (nvs_get_u32(h, KEY_COUNT,   &out->count)       != ESP_OK) out->count = 0;
    if (nvs_get_u32(h, KEY_SPENT,   &out->spent_cf)    != ESP_OK) out->spent_cf = 0;
    if (nvs_get_u32(h, KEY_ELAPSED, &out->elapsed_sec) != ESP_OK) out->elapsed_sec = 0;
    if (nvs_get_u32(h, KEY_REF,     &out->ref_cf)      != ESP_OK) out->ref_cf = 0;
    if (nvs_get_u32(h, KEY_TS,      &out->last_ts)     != ESP_OK) out->last_ts = 0;
    nvs_close(h);
    return true;
}

void store_ledger_save(const ledger_totals_t *t)
{
    if (!t) return;
    nvs_handle_t h;
    if (nvs_open(NS_LEDGER, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, KEY_COUNT,   t->count);
    nvs_set_u32(h, KEY_SPENT,   t->spent_cf);
    nvs_set_u32(h, KEY_ELAPSED, t->elapsed_sec);
    nvs_set_u32(h, KEY_REF,     t->ref_cf);
    nvs_set_u32(h, KEY_TS,      t->last_ts);
    nvs_commit(h);
    nvs_close(h);
}

bool store_holidays_load(void *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NS_HOLIDAYS, NVS_READONLY, &h) != ESP_OK) return false;

    size_t got = len;
    const bool ok = (nvs_get_blob(h, KEY_HOL_BLOB, buf, &got) == ESP_OK) && (got == len);
    nvs_close(h);
    return ok;
}

void store_holidays_save(const void *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NS_HOLIDAYS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, KEY_HOL_BLOB, buf, len);
    nvs_commit(h);
    nvs_close(h);
}
