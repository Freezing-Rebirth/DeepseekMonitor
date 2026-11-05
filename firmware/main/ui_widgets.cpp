// ---------------------------------------------------------------------------
// Shared UI helpers.
//
// The panel is monochrome, so every visual is one of two colours. Keeping that
// mapping in the UI layer (rather than in the driver) lets the whole screen
// invert with one flag when the tariff state flips between peak and off-peak.
// ---------------------------------------------------------------------------
#include "lvgl.h"
#include "ui.h"

lv_obj_t *ui_make_square(lv_obj_t *parent, int size, lv_color_t color, int x, int y)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *ui_make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                        int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, 0, 0);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_pos(l, x, y);
    return l;
}
lv_obj_t *ui_make_box(lv_obj_t *parent, int w, int h, int x, int y,
                      lv_color_t bg, lv_color_t border, int border_w)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    if (border_w > 0) {
        lv_obj_set_style_border_color(o, border, 0);
        lv_obj_set_style_border_width(o, border_w, 0);
    }
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *ui_make_hline(lv_obj_t *parent, int w, int thickness, int x, int y, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, thickness);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *ui_make_vline(lv_obj_t *parent, int h, int thickness, int x, int y, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, thickness, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
