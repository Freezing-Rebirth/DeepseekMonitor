#pragma once

#include "board_rlcd.h"

// Draw the driver bring-up pattern (bypasses LVGL) and push it to the panel.
void selftest_run(RlcdPanel &panel);
