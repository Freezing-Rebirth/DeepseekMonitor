#include "ui.h"
#include "ui_widgets.h"
#include "ui_fonts.h"
#include "pricing.h"
#include "wifi_prov.h"
#include "battery.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <esp_log.h>

static const char *TAG = "ui";

// ---------------------------------------------------------------------------
// Layout reproduced from the four 400x300 exports in
// stitch_pixel_deepseek_status_display/. Coordinates and font sizes are the
// design's own, measured in a real browser (_research/measure_r2.js).
//
// Canvas 400x300, white background, three bands:
//
//   y   0.. 25  h=26   status bar    mark + DEEPSEEK | WIFI BAT clock
//   y  26..149  h=124  balance       TOTAL AVAILABLE FUNDS / RUNWAY
//                                    ¥148.52 / 1.06B TOKENS
//                                    dotted rule
//                                    *BASIS ... / BURN ...
//   y 150..299  h=150  pricing       PEAK or TROUGH (36px)
//                                    NEXT -> 16:30  or  NEXT: 05-21 00:30
//
// Only the pricing band inverts: PEAK is white on black, TROUGH is black on
// white. Earlier revisions inverted the whole panel; the design does not.
//
// Font sizes come from _research/solve_sizes.py, which picks the largest size of
// the proportional Silkscreen Bold face that still fits the width the design
// renders each string at. Most strings match the design's own size exactly.
// ---------------------------------------------------------------------------

// The faces carry the symbols (yen, ~, square, triangle) through their fallback
// chain, so they are fetched at runtime rather than referenced directly. These are
// the Regular (thin) faces, sized up from the Bold set: see ui_fonts.h.
#define F_BODY  (ui_fonts()->body)     // 14 px - brand, funds caption
#define F_SMALL (ui_fonts()->small)    // 12 px - telemetry, runway, basis, burn
#define F_TOKEN (ui_fonts()->token)    // 14 px - 1.06B TOKENS
#define F_BAL   (ui_fonts()->balance)  // 32 px - balance figures
#define F_STATE (ui_fonts()->state)    // 44 px - PEAK / TROUGH
#define F_NEXT  (ui_fonts()->next)     // 18 px - NEXT line

#define INK_BLACK lv_color_black()
#define INK_WHITE lv_color_white()

#define PAD_L 12
#define PAD_R 10

// UTF-8 escapes so this file stays pure ASCII in source.
#define GLYPH_YEN   "\xC2\xA5"       // U+00A5
#define GLYPH_TRI   "\xE2\x96\xB6"   // U+25B6

#define MAX_TRACKED 48

typedef struct {
    lv_obj_t *scr;

    lv_obj_t *bg_status;
    lv_obj_t *bg_balance;
    lv_obj_t *bg_pricing;

    // status bar
    lv_obj_t *st_mark;
    lv_obj_t *st_brand;
    lv_obj_t *st_wifi;
    lv_obj_t *st_bat;
    lv_obj_t *st_clock;

    // balance band
    lv_obj_t *bal_caption;
    lv_obj_t *bal_runway;
    lv_obj_t *bal_yen;
    lv_obj_t *bal_value;
    lv_obj_t *bal_tokens;
    lv_obj_t *bal_basis;
    lv_obj_t *bal_burn;

    // pricing band
    lv_obj_t *pr_state;
    lv_obj_t *pr_next;
    lv_obj_t *pr_arrow;      // the triangle, drawn only in the NEXT -> form

    lv_obj_t *tracked[MAX_TRACKED];
    int       tracked_count;
} ui_refs_t;

static ui_refs_t g;

static void track(lv_obj_t *o)
{
    if (!o) return;
    if (g.tracked_count < MAX_TRACKED) g.tracked[g.tracked_count++] = o;
}

// A label pinned to an explicit box: width from the design, height generous so
// LVGL's own bounds never clip a tall glyph.
static lv_obj_t *boxed(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                       int x, int y, int w, lv_text_align_t align, const char *text)
{
    lv_obj_t *l = ui_make_label(parent, font, color, x, y, text);
    lv_obj_set_width(l, w);
    lv_obj_set_height(l, lv_font_get_line_height(font) + font->base_line + 6);
    lv_obj_set_style_text_align(l, align, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    return l;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
static void build_status()
{
    // No rule under the status bar: the design's top half is continuous, with the
    // only horizontal line at the balance/pricing boundary.
    g.bg_status = ui_make_box(g.scr, UI_WIDTH, 26, 0, 0, INK_WHITE, INK_BLACK, 0);
    track(g.bg_status);

    // Design: 8x8 mark at x=10, y=7; brand text after it at x=20.
    g.st_mark  = ui_make_square(g.scr, 8, INK_BLACK, 10, 9);
    g.st_brand = boxed(g.scr, F_BODY, INK_BLACK, 20, 3, 90, LV_TEXT_ALIGN_LEFT, "DEEPSEEK");
    track(g.st_mark);
    track(g.st_brand);

    // Telemetry, right-aligned to 390. Three separate labels so the gaps match
    // the design's 8 px spacing rather than the font's space width.
    g.st_wifi  = boxed(g.scr, F_SMALL, INK_BLACK, 0, 6, 52, LV_TEXT_ALIGN_LEFT, "WIFI:OK");
    g.st_bat   = boxed(g.scr, F_SMALL, INK_BLACK, 0, 6, 58, LV_TEXT_ALIGN_LEFT, "BAT:--");
    g.st_clock = boxed(g.scr, F_SMALL, INK_BLACK, 0, 6, 100, LV_TEXT_ALIGN_LEFT, "--:-- UTC+8");
    track(g.st_wifi);
    track(g.st_bat);
    track(g.st_clock);
}

static void build_balance()
{
    g.bg_balance = ui_make_box(g.scr, UI_WIDTH, UI_PRICE_Y - 26, 0, 26,
                               INK_WHITE, INK_BLACK, 0);
    lv_obj_set_style_border_side(g.bg_balance, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(g.bg_balance, 2, 0);
    track(g.bg_balance);

    // Line 1
    g.bal_caption = boxed(g.scr, F_BODY, INK_BLACK, PAD_L, 32, 210,
                          LV_TEXT_ALIGN_LEFT, "TOTAL AVAILABLE FUNDS");
    g.bal_runway  = boxed(g.scr, F_SMALL, INK_BLACK, 280, 32, 110,
                          LV_TEXT_ALIGN_RIGHT, "RUNWAY: --D");
    track(g.bal_caption);
    track(g.bal_runway);

    // Line 2: yen from the symbol size, digits at the balance size.
    // Kept as a hidden placeholder; the sign now rides with the figures above.
    g.bal_yen    = boxed(g.scr, F_BAL, INK_BLACK, PAD_L, 56, 16,
                         LV_TEXT_ALIGN_LEFT, "");
    lv_obj_add_flag(g.bal_yen, LV_OBJ_FLAG_HIDDEN);
    g.bal_value  = boxed(g.scr, F_BAL, INK_BLACK, PAD_L, 54, 190,
                         LV_TEXT_ALIGN_LEFT, "--.--");
    g.bal_tokens = boxed(g.scr, F_TOKEN, INK_BLACK, 235, 66, 155,
                         LV_TEXT_ALIGN_RIGHT, "-- TOKENS");
    track(g.bal_yen);
    track(g.bal_value);
    track(g.bal_tokens);

    // Line 4
    g.bal_basis = boxed(g.scr, F_SMALL, INK_BLACK, PAD_L, 116, 160,
                        LV_TEXT_ALIGN_LEFT, "BASIS: \xC2\xA5--.--/M");
    g.bal_burn  = boxed(g.scr, F_SMALL, INK_BLACK, 280, 116, 110,
                        LV_TEXT_ALIGN_RIGHT, "BURN: --");
    track(g.bal_basis);
    track(g.bal_burn);
}

static void build_pricing()
{
    g.bg_pricing = ui_make_box(g.scr, UI_WIDTH, UI_HEIGHT - UI_PRICE_Y, 0, UI_PRICE_Y,
                               INK_WHITE, INK_WHITE, 0);
    track(g.bg_pricing);

    // State word and switch time, shifted 16 px down from the design's position:
    // at the design's y they sat high in the band with a large gap underneath.
    // 166 -> 182 for the 44 px face, 228 -> 244 for the 18 px NEXT line, which
    // keeps 47 px of clearance above and below.
    g.pr_state = boxed(g.scr, F_STATE, INK_BLACK, 0, 182, UI_WIDTH,
                       LV_TEXT_ALIGN_CENTER, "PEAK");
    // The design's copy is literally "NEXT -> 16:30": the arrow is ASCII text, not
    // a symbol. A separate triangle glyph sat on top of the "N". Keep the object
    // for layout compatibility but hidden.
    g.pr_arrow = boxed(g.scr, F_NEXT, INK_BLACK, 0, 244, 16,
                       LV_TEXT_ALIGN_LEFT, GLYPH_TRI);
    lv_obj_add_flag(g.pr_arrow, LV_OBJ_FLAG_HIDDEN);
    g.pr_next  = boxed(g.scr, F_NEXT, INK_BLACK, 0, 244, UI_WIDTH,
                       LV_TEXT_ALIGN_CENTER, "NEXT -> --:--");
    track(g.pr_state);
    track(g.pr_arrow);
    track(g.pr_next);
}

void ui_init(lv_display_t *disp)
{
    ui_fonts_init();
    memset(&g, 0, sizeof(g));
    g.scr = lv_display_get_screen_active(disp);
    lv_obj_remove_style_all(g.scr);
    lv_obj_set_style_bg_color(g.scr, INK_WHITE, 0);
    lv_obj_set_style_bg_opa(g.scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g.scr, LV_OBJ_FLAG_SCROLLABLE);

    build_status();
    build_balance();
    build_pricing();

    ESP_LOGI(TAG, "UI built (design r2): %d objects", g.tracked_count);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
// Compact the balance into ¥ + figures so the currency sign can use a different
// size from the digits, as the design does.
static void fmt_balance(float v, char *digits, size_t n)
{
    snprintf(digits, n, "%.2f", (double)v);
}

// ~1.06B style magnitude for the token equivalent.
static void fmt_tokens(double tokens, char *out, size_t n)
{
    if (tokens >= 1e9)      snprintf(out, n, "%.2fB", tokens / 1e9);
    else if (tokens >= 1e6) snprintf(out, n, "%.2fM", tokens / 1e6);
    else if (tokens >= 1e3) snprintf(out, n, "%.2fK", tokens / 1e3);
    else                    snprintf(out, n, "%.0f", tokens);
}

void ui_invalidate()
{
    if (g.scr) lv_obj_invalidate(g.scr);
}

void ui_set_heartbeat(bool alive)
{
    // The new design has no activity indicator; the parameter is kept so main.cpp
    // does not need changing, and the mark simply stays hidden.
    (void)alive;
}

void ui_update(const ui_model_t *m)
{
    if (!g.scr || !m) return;

    const price_state_t st = m->clock_valid ? pricing_classify(m->now) : PRICE_OFFPEAK;
    const bool peak = (st == PRICE_PEAK);

    // Only the pricing band inverts. PEAK is dark, TROUGH is light.
    const lv_color_t state_bg  = peak ? INK_BLACK : INK_WHITE;
    const lv_color_t state_ink = peak ? INK_WHITE : INK_BLACK;

    lv_obj_set_style_bg_color(g.bg_pricing, state_bg, 0);

    // ---- status bar ----
    char txt[80];
    // Two-character suffix so the column keeps its width as the state changes:
    // TR while an association is in flight, OK once an address is held, ER after a
    // failed attempt or a dropped link.
    snprintf(txt, sizeof(txt), "WIFI:%s", wifi_link_text());
    lv_label_set_text(g.st_wifi, txt);

    if (m->battery_valid) snprintf(txt, sizeof(txt), "BAT:%d%%", m->battery_percent);
    else                  snprintf(txt, sizeof(txt), "BAT:--");
    lv_label_set_text(g.st_bat, txt);

    if (m->clock_valid) {
        // The clock is shown in Beijing time. pricing_beijing_tm() already applies
        // the +8 offset, so the label has to say UTC+8 - labelling it "UTC" while
        // displaying Beijing time was simply wrong.
        struct tm bj;
        pricing_beijing_tm(m->now, &bj);
        snprintf(txt, sizeof(txt), "%02d:%02d UTC+8", bj.tm_hour, bj.tm_min);
    } else {
        snprintf(txt, sizeof(txt), "--:-- UTC+8");
    }
    lv_label_set_text(g.st_clock, txt);

    // Right-align the three telemetry labels with the design's 8 px gaps.
    // Right-aligned to the content edge. The box must be wide enough for its own
    // text and must not butt against the panel edge, or the trailing "8" of
    // "UTC+8" gets clipped.
    lv_obj_set_pos(g.st_clock, 0, 0);
    lv_obj_align(g.st_clock, LV_ALIGN_TOP_RIGHT, -3, 6);
    lv_obj_align_to(g.st_bat, g.st_clock, LV_ALIGN_OUT_LEFT_TOP, -8, 0);
    lv_obj_align_to(g.st_wifi, g.st_bat, LV_ALIGN_OUT_LEFT_TOP, -8, 0);

    // ---- balance band ----
    // The currency sign and the figures share one label at one size. Splitting
    // them across two sizes (10 px sign, 20 px digits) let LVGL baseline-align
    // mixed runs and pushed the sign off the digits.
    if (m->balance_valid) {
        char digits[24];
        fmt_balance(m->balance_cny, digits, sizeof(digits));
        snprintf(txt, sizeof(txt), GLYPH_YEN "%s", digits);
    } else {
        snprintf(txt, sizeof(txt), GLYPH_YEN "--.--");
    }
    lv_label_set_text(g.bal_value, txt);

    // Both the value and the sign need their final placement AFTER set_text,
    // because setting a label's text re-runs its layout and would otherwise undo
    // any alignment applied before it.
    lv_obj_set_pos(g.bal_value, 0, 0);
    lv_obj_align(g.bal_value, LV_ALIGN_TOP_LEFT, PAD_L, 62);

    // The design right-aligns the token figure to x=390.
    lv_obj_set_pos(g.bal_tokens, 0, 0);
    lv_obj_align(g.bal_tokens, LV_ALIGN_TOP_RIGHT, -PAD_R, 74);
    lv_obj_set_style_text_align(g.bal_tokens, LV_TEXT_ALIGN_RIGHT, 0);

    if (m->ledger_valid && m->runway_days > 0.0f) {
        snprintf(txt, sizeof(txt), "RUNWAY: %.0fD", (double)m->runway_days);
    } else {
        snprintf(txt, sizeof(txt), "RUNWAY: --D");
    }
    lv_label_set_text(g.bal_runway, txt);
    // Re-anchor after set_text, for the same layout reason as the balance.
    lv_obj_set_pos(g.bal_runway, 0, 0);
    lv_obj_align(g.bal_runway, LV_ALIGN_TOP_RIGHT, -PAD_R, 32);
    lv_obj_set_style_text_align(g.bal_runway, LV_TEXT_ALIGN_RIGHT, 0);

    // Token equivalent: balance divided by the current period's cache-hit input
    // price, which is what the panel displays as "*BASIS ... IN-CACHE".
    const price_pair_t pair = pricing_lookup(m->model, m->tier);
    const float rate = peak ? pair.peak : pair.offpeak;
    if (m->balance_valid && rate > 0.0001f) {
        const double tokens = (double)m->balance_cny / (double)rate * 1e6;
        char mag[24];
        fmt_tokens(tokens, mag, sizeof(mag));
        snprintf(txt, sizeof(txt), "%s TOKENS", mag);
    } else {
        snprintf(txt, sizeof(txt), "-- TOKENS");
    }
    lv_label_set_text(g.bal_tokens, txt);

    if (m->ledger_valid && m->daily_burn_cny > 0.0f) {
        snprintf(txt, sizeof(txt), "BURN: \xC2\xA5%.2f/D", (double)m->daily_burn_cny);
    } else {
        snprintf(txt, sizeof(txt), "BURN: --");
    }
    lv_label_set_text(g.bal_burn, txt);
    lv_obj_set_pos(g.bal_burn, 0, 0);
    lv_obj_align(g.bal_burn, LV_ALIGN_TOP_RIGHT, -PAD_R, 116);
    lv_obj_set_style_text_align(g.bal_burn, LV_TEXT_ALIGN_RIGHT, 0);

    // Basis line: the rate in force, in CNY per million tokens. The design's
    // "V3 IN-CACHE" and the leading asterisk are gone - the model and tier are not
    // shown - but the "BASIS:" label stays so the figure is identifiable.
    snprintf(txt, sizeof(txt), "BASIS: \xC2\xA5%.2f/M", (double)rate);
    lv_label_set_text(g.bal_basis, txt);

    // ---- pricing band ----
    lv_obj_set_style_text_color(g.pr_state, state_ink, 0);
    lv_obj_set_style_text_color(g.pr_next, state_ink, 0);
    lv_obj_set_style_text_color(g.pr_arrow, state_ink, 0);

    lv_label_set_text(g.pr_state, peak ? "PEAK" : "TROUGH");

    if (m->clock_valid) {
        const int64_t secs = pricing_seconds_to_next_change(m->now);
        struct tm bj;
        pricing_beijing_tm(m->now + secs, &bj);
        const struct tm now_bj = [&] { struct tm t; pricing_beijing_tm(m->now, &t); return t; }();
        const bool same_day = (bj.tm_yday == now_bj.tm_yday);
        if (same_day) {
            // Today: the design omits the date.
            snprintf(txt, sizeof(txt), "NEXT -> %02d:%02d", bj.tm_hour, bj.tm_min);
            lv_obj_add_flag(g.pr_arrow, LV_OBJ_FLAG_HIDDEN);
        } else {
            snprintf(txt, sizeof(txt), "NEXT: %02d-%02d %02d:%02d",
                     bj.tm_mon + 1, bj.tm_mday, bj.tm_hour, bj.tm_min);
            lv_obj_add_flag(g.pr_arrow, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        snprintf(txt, sizeof(txt), "NEXT -> --:--");
    }
    lv_label_set_text(g.pr_next, txt);

    lv_obj_invalidate(g.scr);
}
