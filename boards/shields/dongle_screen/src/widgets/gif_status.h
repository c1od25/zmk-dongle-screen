/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

/* GIF animation widget (plays 96fx96f.gif, downscaled 70x70, in the former
 * WPM cell). Mutually exclusive with WPM and the scanner widget via Kconfig. */
struct zmk_widget_gif_status
{
    lv_obj_t *obj;
};

int zmk_widget_gif_status_init(struct zmk_widget_gif_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_gif_status_obj(struct zmk_widget_gif_status *widget);
