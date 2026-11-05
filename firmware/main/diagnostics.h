#pragma once

#include <freertos/FreeRTOS.h>

// Developer-facing liveness and consumption telemetry. Writes nothing in a release
// build: the body is compiled out unless DEBUG_LOGS is on.
//
// The scheduler's remaining deadlines are passed in rather than recomputed here, so
// this file needs to know nothing about how the loop schedules its work.
void diagnostics_heartbeat(TickType_t last_ui, TickType_t last_api);
