/*******************************************************************************
 * Size: 40 px
 * Bpp: 2
 * Opts: --bpp 2 --size 40 --no-compress --no-prefilter --no-kerning --format lvgl --lv-include lvgl.h --font /home/df1080/.local/share/fonts/JetBrainsMono/JetBrainsMonoNerdFontMono-Bold.ttf -r 0xF04C5 -o NerdFonts_Speedo_40.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef NERDFONTS_SPEEDO_40
#define NERDFONTS_SPEEDO_40 1
#endif

#if NERDFONTS_SPEEDO_40

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+F04C5 "󰓅" */
    0x0, 0x0, 0x6b, 0xe9, 0x0, 0x0, 0x0, 0x7,
    0xff, 0xff, 0xd0, 0x0, 0x0, 0x3f, 0xfa, 0xaf,
    0x80, 0x0, 0x1, 0xfe, 0x0, 0x0, 0x1, 0xc0,
    0x3, 0xf4, 0x0, 0x0, 0xf, 0x80, 0xf, 0xc0,
    0x0, 0x0, 0xbf, 0x0, 0x2f, 0x40, 0x0, 0x7,
    0xfd, 0x4, 0x3e, 0x0, 0x0, 0x7f, 0xfc, 0x1c,
    0x7c, 0x0, 0x2, 0xff, 0xf0, 0x3d, 0xbc, 0x0,
    0x1f, 0xff, 0xe0, 0x3e, 0xf8, 0x0, 0x3f, 0xff,
    0xc0, 0x2f, 0xf4, 0x0, 0xbf, 0xff, 0x80, 0x1f,
    0xf4, 0x0, 0xbf, 0xff, 0x0, 0x1f, 0xf8, 0x0,
    0x7f, 0xfd, 0x0, 0x2f, 0xf8, 0x0, 0x2f, 0xf8,
    0x0, 0x2f, 0x7c, 0x0, 0x6, 0x90, 0x0, 0x3d,
    0x3e, 0x0, 0x0, 0x0, 0x0, 0xbc, 0x2f, 0x0,
    0x0, 0x0, 0x0, 0xf8, 0xf, 0xc0, 0x0, 0x0,
    0x3, 0xf0, 0x7, 0xe0, 0x0, 0x0, 0x7, 0xd0,
    0x2, 0xd0, 0x0, 0x0, 0x7, 0x80, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 384, .box_w = 24, .box_h = 22, .ofs_x = 0, .ofs_y = 3}
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
const lv_font_t NerdFonts_Speedo_40 = {
#else
lv_font_t NerdFonts_Speedo_40 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 22,          /*The maximum line height required by the font*/
    .base_line = -3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -6,
    .underline_thickness = 2,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if NERDFONTS_SPEEDO_40*/

