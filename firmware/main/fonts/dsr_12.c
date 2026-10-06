/*******************************************************************************
 * Size: 12 px
 * Bpp: 1
 * Opts: --bpp 1 --size 12 --font _research/fonts/Silkscreen-Regular.ttf --format lvgl --no-compress --no-prefilter --no-kerning --force-fast-kern-format --lv-include lvgl.h --lv-font-name dsr_12 -o firmware\main\fonts\dsr_12.c -r 0x20-0x7E -r 0xB0 -r 0xA5
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef DSR_12
#define DSR_12 1
#endif

#if DSR_12

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0xc3,

    /* U+0022 "\"" */
    0xde, 0xf6,

    /* U+0023 "#" */
    0x6c, 0x6c, 0xff, 0x6c, 0x6c, 0xff, 0x6c, 0x6c,

    /* U+0024 "$" */
    0x18, 0x63, 0xc0, 0xc0, 0xe0, 0x81, 0xb, 0xe2,
    0x8,

    /* U+0025 "%" */
    0xe4, 0xe4, 0xe4, 0x18, 0x8, 0x67, 0x67, 0x67,

    /* U+0026 "&" */
    0x18, 0x30, 0xfa, 0xc, 0x7, 0x10, 0x60, 0x40,
    0x7c, 0x60, 0xc0,

    /* U+0027 "'" */
    0xfc,

    /* U+0028 "(" */
    0x23, 0x6d, 0xb1,

    /* U+0029 ")" */
    0xc8, 0x92, 0x4e,

    /* U+002A "*" */
    0x18, 0x18, 0xdb, 0x3c, 0x5c, 0xdb, 0x18, 0x18,

    /* U+002B "+" */
    0x18, 0x18, 0x18, 0xff, 0x18, 0x18, 0x18, 0x18,

    /* U+002C "," */
    0x38,

    /* U+002D "-" */
    0xf8,

    /* U+002E "." */
    0xc0,

    /* U+002F "/" */
    0xc, 0x30, 0xc8, 0x3, 0xc, 0x30,

    /* U+0030 "0" */
    0x39, 0x2c, 0x71, 0xc7, 0x1c, 0x4e,

    /* U+0031 "1" */
    0xe3, 0x18, 0xc6, 0x31, 0x9f,

    /* U+0032 "2" */
    0xf8, 0x20, 0x4e, 0xc3, 0xc, 0x3f,

    /* U+0033 "3" */
    0xf8, 0x20, 0x4e, 0x8, 0x10, 0x3e,

    /* U+0034 "4" */
    0xdb, 0x6d, 0xbf, 0x18, 0x61, 0x86,

    /* U+0035 "5" */
    0xff, 0xc, 0x3e, 0x8, 0x10, 0x3e,

    /* U+0036 "6" */
    0x3b, 0xc, 0x30, 0xfb, 0x14, 0x8e,

    /* U+0037 "7" */
    0xfe, 0xc, 0x18, 0x41, 0x6, 0xc, 0x18,

    /* U+0038 "8" */
    0x39, 0x2c, 0x4e, 0x4b, 0x14, 0x8e,

    /* U+0039 "9" */
    0x39, 0x2c, 0x4f, 0x4, 0x10, 0x4e,

    /* U+003A ":" */
    0xc0, 0xc0,

    /* U+003B ";" */
    0x20, 0x11, 0x80,

    /* U+003C "<" */
    0xc, 0x2, 0x30, 0x40, 0x81, 0x3,

    /* U+003D "=" */
    0xf8, 0x0, 0xf, 0x80,

    /* U+003E ">" */
    0xc1, 0x2, 0x7, 0x0, 0x80, 0x30,

    /* U+003F "?" */
    0xf0, 0x40, 0xdc, 0x0, 0x0, 0x18,

    /* U+0040 "@" */
    0x3c, 0x1c, 0xdb, 0xdc, 0xc0, 0xc0, 0xc0, 0x3c,

    /* U+0041 "A" */
    0x39, 0x2c, 0x7f, 0xc7, 0x1c, 0x71,

    /* U+0042 "B" */
    0xf3, 0x4c, 0xff, 0xcf, 0x3c, 0xfc,

    /* U+0043 "C" */
    0x38, 0x2c, 0x70, 0xc3, 0x14, 0xe,

    /* U+0044 "D" */
    0xf3, 0x4c, 0xf3, 0xcf, 0x3c, 0xfc,

    /* U+0045 "E" */
    0xfe, 0x31, 0xfc, 0x63, 0x1f,

    /* U+0046 "F" */
    0xfe, 0x31, 0xfc, 0x63, 0x18,

    /* U+0047 "G" */
    0x3c, 0xc, 0x37, 0xc7, 0x1c, 0x4e,

    /* U+0048 "H" */
    0xcf, 0x3c, 0xff, 0xcf, 0x3c, 0xf3,

    /* U+0049 "I" */
    0xff, 0xff,

    /* U+004A "J" */
    0x4, 0x10, 0x41, 0x7, 0x14, 0xe,

    /* U+004B "K" */
    0xc7, 0xd, 0xb8, 0xe3, 0x6c, 0xb1,

    /* U+004C "L" */
    0xc6, 0x31, 0x8c, 0x63, 0x1f,

    /* U+004D "M" */
    0xc3, 0xc3, 0xe7, 0xdb, 0xc3, 0xc3, 0xc3, 0xc3,

    /* U+004E "N" */
    0xc3, 0xc3, 0xe3, 0xdb, 0xcb, 0xc7, 0xc3, 0xc3,

    /* U+004F "O" */
    0x39, 0x2c, 0x71, 0xc7, 0x1c, 0x4e,

    /* U+0050 "P" */
    0xf3, 0x4c, 0xfc, 0xc3, 0xc, 0x30,

    /* U+0051 "Q" */
    0x39, 0x2c, 0x71, 0xc7, 0x1c, 0x4e, 0x4,

    /* U+0052 "R" */
    0xfb, 0x2c, 0x7e, 0xdb, 0x6d, 0xb1,

    /* U+0053 "S" */
    0x3c, 0xc, 0xe, 0x8, 0x10, 0x3e,

    /* U+0054 "T" */
    0xfb, 0x18, 0xc6, 0x31, 0x8c,

    /* U+0055 "U" */
    0xc7, 0x1c, 0x71, 0xc7, 0x14, 0xe,

    /* U+0056 "V" */
    0xc3, 0xc3, 0xc3, 0x24, 0x24, 0x24, 0x20, 0x18,

    /* U+0057 "W" */
    0xc3, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0x48, 0x24,

    /* U+0058 "X" */
    0xc3, 0x44, 0x24, 0x18, 0x8, 0x24, 0x4, 0xc3,

    /* U+0059 "Y" */
    0xc3, 0x40, 0x24, 0x18, 0x18, 0x18, 0x18, 0x18,

    /* U+005A "Z" */
    0xfc, 0x30, 0xc8, 0xc3, 0xc, 0x3f,

    /* U+005B "[" */
    0xfb, 0x6d, 0xb7,

    /* U+005C "\\" */
    0xc3, 0xc, 0x8, 0x10, 0x30, 0xc3,

    /* U+005D "]" */
    0xf3, 0x33, 0x33, 0x3f,

    /* U+005E "^" */
    0x21, 0x4c, 0xc0,

    /* U+005F "_" */
    0xfc,

    /* U+0060 "`" */
    0xc8, 0x80,

    /* U+0061 "a" */
    0x39, 0x2c, 0x7f, 0xc7, 0x1c, 0x71,

    /* U+0062 "b" */
    0xf3, 0x4c, 0xff, 0xcf, 0x3c, 0xfc,

    /* U+0063 "c" */
    0x38, 0x2c, 0x70, 0xc3, 0x14, 0xe,

    /* U+0064 "d" */
    0xf3, 0x4c, 0xf3, 0xcf, 0x3c, 0xfc,

    /* U+0065 "e" */
    0xfe, 0x31, 0xfc, 0x63, 0x1f,

    /* U+0066 "f" */
    0xfe, 0x31, 0xfc, 0x63, 0x18,

    /* U+0067 "g" */
    0x3c, 0xc, 0x37, 0xc7, 0x1c, 0x4e,

    /* U+0068 "h" */
    0xcf, 0x3c, 0xff, 0xcf, 0x3c, 0xf3,

    /* U+0069 "i" */
    0xff, 0xff,

    /* U+006A "j" */
    0x4, 0x10, 0x41, 0x7, 0x14, 0xe,

    /* U+006B "k" */
    0xc7, 0xd, 0xb8, 0xe3, 0x6c, 0xb1,

    /* U+006C "l" */
    0xc6, 0x31, 0x8c, 0x63, 0x1f,

    /* U+006D "m" */
    0xc3, 0xc3, 0xe7, 0xdb, 0xc3, 0xc3, 0xc3, 0xc3,

    /* U+006E "n" */
    0xc3, 0xc3, 0xe3, 0xdb, 0xcb, 0xc7, 0xc3, 0xc3,

    /* U+006F "o" */
    0x39, 0x2c, 0x71, 0xc7, 0x1c, 0x4e,

    /* U+0070 "p" */
    0xf3, 0x4c, 0xfc, 0xc3, 0xc, 0x30,

    /* U+0071 "q" */
    0x39, 0x2c, 0x71, 0xc7, 0x1c, 0x4e, 0x4,

    /* U+0072 "r" */
    0xfb, 0x2c, 0x7e, 0xdb, 0x6d, 0xb1,

    /* U+0073 "s" */
    0x3c, 0xc, 0xe, 0x8, 0x10, 0x3e,

    /* U+0074 "t" */
    0xfb, 0x18, 0xc6, 0x31, 0x8c,

    /* U+0075 "u" */
    0xc7, 0x1c, 0x71, 0xc7, 0x14, 0xe,

    /* U+0076 "v" */
    0xc3, 0xc3, 0xc3, 0x24, 0x24, 0x24, 0x20, 0x18,

    /* U+0077 "w" */
    0xc3, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0x48, 0x24,

    /* U+0078 "x" */
    0xc3, 0x44, 0x24, 0x18, 0x8, 0x24, 0x4, 0xc3,

    /* U+0079 "y" */
    0xc3, 0x40, 0x24, 0x18, 0x18, 0x18, 0x18, 0x18,

    /* U+007A "z" */
    0xfc, 0x30, 0xc8, 0xc3, 0xc, 0x3f,

    /* U+007B "{" */
    0x39, 0x9, 0x82, 0x10, 0x87,

    /* U+007C "|" */
    0xff, 0xff, 0xfc,

    /* U+007D "}" */
    0xf1, 0x8c, 0x13, 0x18, 0xde,

    /* U+007E "~" */
    0x25, 0x8d, 0x80,

    /* U+00A5 "¥" */
    0xc3, 0x40, 0x3c, 0x18, 0x18, 0x3c, 0x18, 0x18,

    /* U+00B0 "°" */
    0x66, 0xd8, 0xc0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 96, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 72, .box_w = 2, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 3, .adv_w = 120, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 5, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 13, .adv_w = 144, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 22, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 30, .adv_w = 144, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 41, .adv_w = 72, .box_w = 2, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 42, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 45, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 48, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 56, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 64, .adv_w = 96, .box_w = 3, .box_h = 2, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 65, .adv_w = 120, .box_w = 5, .box_h = 1, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 66, .adv_w = 72, .box_w = 2, .box_h = 1, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 67, .adv_w = 120, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 73, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 79, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 90, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 96, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 102, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 108, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 114, .adv_w = 144, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 121, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 127, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 133, .adv_w = 72, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 135, .adv_w = 96, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 120, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 144, .adv_w = 120, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 148, .adv_w = 120, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 154, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 160, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 168, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 174, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 180, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 186, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 192, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 197, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 202, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 208, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 214, .adv_w = 72, .box_w = 2, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 216, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 228, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 233, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 241, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 249, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 255, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 261, .adv_w = 144, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 268, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 274, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 280, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 285, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 291, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 299, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 307, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 315, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 323, .adv_w = 120, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 329, .adv_w = 96, .box_w = 3, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 332, .adv_w = 120, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 338, .adv_w = 96, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 342, .adv_w = 120, .box_w = 6, .box_h = 3, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 345, .adv_w = 144, .box_w = 6, .box_h = 1, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 346, .adv_w = 96, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 10},
    {.bitmap_index = 348, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 354, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 360, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 366, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 372, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 377, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 382, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 388, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 394, .adv_w = 72, .box_w = 2, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 396, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 402, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 408, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 413, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 421, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 429, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 435, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 441, .adv_w = 144, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 448, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 454, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 460, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 465, .adv_w = 144, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 471, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 479, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 487, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 495, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 503, .adv_w = 120, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 509, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 514, .adv_w = 72, .box_w = 2, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 517, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 522, .adv_w = 144, .box_w = 6, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 525, .adv_w = 168, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 533, .adv_w = 120, .box_w = 5, .box_h = 4, .ofs_x = 1, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_1[] = {
    0x0, 0xb
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 165, .range_length = 12, .glyph_id_start = 96,
        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 2,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t dsr_12 = {
#else
lv_font_t dsr_12 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if DSR_12*/

