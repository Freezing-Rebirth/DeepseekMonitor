#pragma once

#include <stdbool.h>
#include <stddef.h>

// Runtime configuration facade. Values come from NVS, with build-time
// defaults from app_config.h as a fallback.
void app_config_init();

// Fetch the DeepSeek API key. Returns false when none is configured.
bool app_config_api_key(char *out, size_t out_len);

// Store a new API key and update the in-memory copy.
void app_config_set_api_key(const char *key);
