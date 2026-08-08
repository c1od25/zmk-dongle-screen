/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <lvgl.h>

#include "gif_status.h"

/* 96fx96f.gif downscaled to 70x70, 55 frames @ 40ms, embedded as a C array.
 * The lv_gif decoder parses the raw GIF bytes on the fly and animates via its
 * own internal timer (no widget-side timer needed). */
extern const unsigned char gif_data[];
extern const unsigned int gif_data_len;

#define GIF_W 70
#define GIF_H 70

#if CONFIG_DONGLE_SCREEN_HORIZONTAL
#define GIF_CELL_X 60
#define GIF_CELL_Y 174
#define GIF_CELL_W 200
#define GIF_CELL_H 56
#define GIF_X ((GIF_CELL_W - GIF_W) / 2) /* 65 */
#define GIF_Y ((GIF_CELL_H - GIF_H) / 2) /* -7: 70px gif clips 7px top/bottom */
#else
#define GIF_CELL_X 66
#define GIF_CELL_Y 226
#define GIF_CELL_W 108
#define GIF_CELL_H 82
#define GIF_X ((GIF_CELL_W - GIF_W) / 2) /* 19 */
#define GIF_Y ((GIF_CELL_H - GIF_H) / 2) /* 6 */
#endif

static const lv_image_dsc_t gif_img_dsc = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC},
    .data = gif_data,
    .data_size = gif_data_len,
};

int zmk_widget_gif_status_init(struct zmk_widget_gif_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(widget->obj, GIF_CELL_X, GIF_CELL_Y);
    lv_obj_set_size(widget->obj, GIF_CELL_W, GIF_CELL_H);

    lv_obj_t *gif = lv_gif_create(widget->obj);
    lv_gif_set_src(gif, &gif_img_dsc);
    lv_obj_set_size(gif, GIF_W, GIF_H);
    lv_obj_set_pos(gif, GIF_X, GIF_Y);

    return 0;
}

lv_obj_t *zmk_widget_gif_status_obj(struct zmk_widget_gif_status *widget)
{
    return widget->obj;
}
