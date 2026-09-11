/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_showkey_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *label;      /* Mono_48: text keys, or the L/R prefix for mods */
    lv_obj_t *icon_label; /* NerdFonts_Regular_48: modifier/arrow/special icons */
    lv_timer_t *hold_timer;
};

int zmk_widget_showkey_status_init(struct zmk_widget_showkey_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_showkey_status_obj(struct zmk_widget_showkey_status *widget);
