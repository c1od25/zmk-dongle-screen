/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define WPM_BLOCK_N 15

/* Block-character scanner animation in the former WPM cell. The widget is a
 * pure animation (no ZMK events): 15 discrete square blocks with a 6-level
 * exponential trail, scanning forward/backward with 4- and 12-frame gaps. */
struct zmk_widget_wpm_status
{
    lv_obj_t *obj;
    lv_obj_t *blocks[WPM_BLOCK_N];
    sys_snode_t node;
};

int zmk_widget_wpm_status_init(struct zmk_widget_wpm_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_wpm_status_obj(struct zmk_widget_wpm_status *widget);
