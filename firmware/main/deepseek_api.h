#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Thin client for the DeepSeek open platform.
//
//   GET https://api.deepseek.com/user/balance
//   Authorization: Bearer <API key>
//
// Response shape:
//   {
//     "is_available": true,
//     "balance_infos": [
//       { "currency": "CNY",
//         "total_balance": "148.52",
//         "granted_balance": "0.00",
//         "topped_up_balance": "148.52" }
//     ]
//   }
// ---------------------------------------------------------------------------

typedef struct {
    bool  ok;                    // request + parse succeeded
    bool  is_available;          // account able to serve requests
    char  currency[8];           // "CNY" / "USD"
    float total_balance;         // total available, incl. granted + topped up
    float granted_balance;       // unexpired grant balance
    float topped_up_balance;     // top-up balance
    int   http_status;           // HTTP status, 0 when the request never landed
    char  error[96];             // human readable failure reason
} ds_balance_t;

// Perform one balance query. `api_key` must be non-empty.
ds_balance_t ds_query_balance(const char *api_key, int timeout_ms);
