#pragma once

#include "ui.h"

// Build the one structure ui_update() consumes.
//
// This is the panel's only entrée to the rest of the application: the UI draws what it
// is given and asks no subsystem anything. Everything the panel cannot show is left
// out of ui_model_t, so "the model has a field nobody reads" cannot happen again.
//
// Caller must hold the LVGL mutex only for the ui_update() that follows; building the
// model touches no LVGL object.
void panel_model_build(ui_model_t *m);
