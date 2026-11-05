#pragma once

// Parsing the /user/balance response.
//
// Pure apart from cJSON: no HTTP, no logging, no configuration. deepseek_api.cpp owns the
// request; this owns what the answer means, and is tested against real-shaped payloads on
// the host.

#include <stdbool.h>
#include <stddef.h>

#include "deepseek_api.h"

// Parse a response body into `out`. Returns false and fills out->error on anything that
// is not the documented shape. Fields absent from the payload are left at zero.
bool balance_json_parse(const char *json, size_t len, ds_balance_t *out);
