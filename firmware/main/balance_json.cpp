// Parsing the /user/balance response. See balance_json.h.

#include "balance_json.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <cJSON.h>

bool balance_json_parse(const char *json, size_t len, ds_balance_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        snprintf(out->error, sizeof(out->error), "JSON parse failed");
        return false;
    }

    const cJSON *avail = cJSON_GetObjectItemCaseSensitive(root, "is_available");
    if (cJSON_IsBool(avail)) {
        out->is_available = cJSON_IsTrue(avail);
    }

    const cJSON *infos = cJSON_GetObjectItemCaseSensitive(root, "balance_infos");
    if (!cJSON_IsArray(infos) || cJSON_GetArraySize(infos) == 0) {
        snprintf(out->error, sizeof(out->error), "balance_infos missing");
        cJSON_Delete(root);
        return false;
    }

    // Prefer the CNY entry; fall back to the first one.
    const cJSON *chosen = nullptr;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, infos) {
        const cJSON *cur = cJSON_GetObjectItemCaseSensitive(item, "currency");
        if (cJSON_IsString(cur) && strcmp(cur->valuestring, "CNY") == 0) {
            chosen = item;
            break;
        }
        if (!chosen) chosen = item;
    }
    if (!chosen) chosen = cJSON_GetArrayItem(infos, 0);

    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(chosen, "currency");
    if (cJSON_IsString(cur)) {
        snprintf(out->currency, sizeof(out->currency), "%s", cur->valuestring);
    }

    const cJSON *total = cJSON_GetObjectItemCaseSensitive(chosen, "total_balance");
    const cJSON *grant = cJSON_GetObjectItemCaseSensitive(chosen, "granted_balance");
    const cJSON *topup = cJSON_GetObjectItemCaseSensitive(chosen, "topped_up_balance");

    // Values normally arrive as strings, e.g. "148.52"; accept numbers too, because a
    // schema change there would otherwise read as a zero balance rather than an error.
    if (cJSON_IsString(total)) out->total_balance = strtof(total->valuestring, nullptr);
    else if (cJSON_IsNumber(total)) out->total_balance = (float)total->valuedouble;
    if (cJSON_IsString(grant)) out->granted_balance = strtof(grant->valuestring, nullptr);
    if (cJSON_IsString(topup)) out->topped_up_balance = strtof(topup->valuestring, nullptr);

    cJSON_Delete(root);
    return true;
}
