/*******************************************************************************
 * Size: 28 px
 * Bpp: 2
 * Opts: --bpp 2 --size 28 --no-compress --no-prefilter --no-kerning --format lvgl --lv-include lvgl.h --font /home/df1080/.local/share/fonts/JetBrainsMono/JetBrainsMonoNerdFontMono-Regular.ttf -r 0xF04C5 -o NerdFonts_Regular_28.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef NERDFONTS_REGULAR_28
#define NERDFONTS_REGULAR_28 1
#endif

#if NERDFONTS_REGULAR_28

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+F04C5 "󰓅" */
    0x0, 0x5, 0x94, 0x0, 0x0, 0x2f, 0xff, 0xc0,
    0x0, 0x3f, 0x50, 0x40, 0x80, 0x3d, 0x0, 0x1,
    0xd0, 0x3d, 0x0, 0x7, 0xf0, 0x1e, 0x0, 0xb,
    0xf0, 0xcb, 0x0, 0x2f, 0xf8, 0x37, 0xc0, 0x2f,
    0xfc, 0xe, 0xe0, 0xf, 0xfe, 0x3, 0xfc, 0x3,
    0xff, 0x0, 0xef, 0x0, 0x7f, 0x0, 0x3a, 0xd0,
    0x1, 0x0, 0x1d, 0x3c, 0x0, 0x0, 0xf, 0xb,
    0x80, 0x0, 0xb, 0x40, 0xa0, 0x0, 0x3, 0x80,
    0x0, 0x0, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 269, .box_w = 17, .box_h = 16, .ofs_x = 0, .ofs_y = 2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 984261, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
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
    .cmap_num = 1,
    .bpp = 2,
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
const lv_font_t NerdFonts_Regular_28 = {
#else
lv_font_t NerdFonts_Regular_28 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 16,          /*The maximum line height required by the font*/
    .base_line = -2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -4,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if NERDFONTS_REGULAR_28*/

