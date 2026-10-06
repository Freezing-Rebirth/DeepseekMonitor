// ---------------------------------------------------------------------------
// Font plumbing.
//
// LVGL's font structs are const, so the symbol faces cannot be attached to them
// in place. This makes mutable copies of the text faces with `fallback` pointing
// at the matching symbols-only face, which is LVGL's own mechanism for resolving a
// glyph a font does not carry.
// ---------------------------------------------------------------------------
#include "ui_fonts.h"

#include <string.h>
#include <esp_log.h>

static const char *TAG = "fonts";

static lv_font_t s_body;
static lv_font_t s_small;
static lv_font_t s_token;
static lv_font_t s_balance;
static lv_font_t s_state;
static lv_font_t s_next;

static struct dsb_fonts s_fonts;

void ui_fonts_init()
{
    s_body    = dsr_14;  s_body.fallback    = &dsy_14;
    s_small   = dsr_12;  s_small.fallback   = &dsy_12;
    s_token   = dsr_14;  s_token.fallback   = &dsy_14;
    s_balance = dsr_32;  s_balance.fallback = &dsy_32;
    s_state   = dsr_44;  s_state.fallback   = &dsy_44;
    s_next    = dsr_18;  s_next.fallback    = &dsy_18;

    s_fonts.body    = &s_body;
    s_fonts.small   = &s_small;
    s_fonts.token   = &s_token;
    s_fonts.balance = &s_balance;
    s_fonts.state   = &s_state;
    s_fonts.next    = &s_next;

    ESP_LOGI(TAG, "fonts ready: 6 faces with symbol fallbacks "
                  "(body lh=%d bl=%d)",
             (int)s_body.line_height, (int)s_body.base_line);
}

const struct dsb_fonts *ui_fonts()
{
    return &s_fonts;
}
