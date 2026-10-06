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

// Right-aligned label: x is measured from the panel's right edge.
lv_obj_t *ui_make_label_right(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                              int right_pad, int y, const char *text)
{
    lv_obj_t *l = ui_make_label(parent, font, color, 0, y, text);
    lv_obj_set_width(l, UI_WIDTH - right_pad);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(l, 0, y);
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

void ui_make_dashed_rule(lv_obj_t *parent, int x, int y, int w,
                         int dash, int gap, lv_color_t color, lv_obj_t **out, int max_out,
                         int *count)
{
    int n = 0;
    for (int px = x; px + dash <= x + w; px += dash + gap) {
        lv_obj_t *seg = ui_make_hline(parent, dash, 1, px, y, color);
        if (out && n < max_out) out[n] = seg;
        n++;
    }
    if (count) *count = n;
}
