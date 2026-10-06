#pragma once

#include "board_rlcd.h"

// ---------------------------------------------------------------------------
// Frame dump over the console serial port.
//
// The board is the only place that can produce the real panel frame, so for
// development the 1bpp frame buffer is streamed out as hex and decoded on a
// workstation into a PNG. This verifies the actual pixel mapping, the LVGL
// flush path and the layout without a camera pointed at the panel.
//
// Wrap the output with the markers below and strip the console log lines.
// ---------------------------------------------------------------------------
void frame_dump_start(RlcdPanel &panel);
