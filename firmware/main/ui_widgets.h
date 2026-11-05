#pragma once

#include "lvgl.h"

// Shared monochrome widget helpers (see ui_widgets.cpp).
lv_obj_t *ui_make_square(lv_obj_t *parent, int size, lv_color_t color, int x, int y);
lv_obj_t *ui_make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                        int x, int y, const char *text);

lv_obj_t *ui_make_box(lv_obj_t *parent, int w, int h, int x, int y,
                      lv_color_t bg, lv_color_t border, int border_w);
lv_obj_t *ui_make_hline(lv_obj_t *parent, int w, int thickness, int x, int y, lv_color_t color);
lv_obj_t *ui_make_vline(lv_obj_t *parent, int h, int thickness, int x, int y, lv_color_t color);
