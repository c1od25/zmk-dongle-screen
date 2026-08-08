/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define SCANNER_BLOCK_N 15

/* Block-character scanning loading animation (kr_4_12 rhythm). Occupies the
 * same screen cell as the WPM widget but is a pure animation (no ZMK events).
 * Mutually exclusive with WPM via Kconfig. */
struct zmk_widget_scanner_status
{
    lv_obj_t *obj;
    lv_obj_t *blocks[SCANNER_BLOCK_N];
    sys_snode_t node;
};

int zmk_widget_scanner_status_init(struct zmk_widget_scanner_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_scanner_status_obj(struct zmk_widget_scanner_status *widget);
