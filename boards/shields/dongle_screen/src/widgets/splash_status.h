/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

/* Boot splash: a full-screen overlay shown once on startup. It covers the
 * freshly assembled status screen, holds for SPLASH_HOLD_MS, then fades out
 * to reveal the main UI. Runs entirely on the display thread during
 * zmk_display_status_screen() assembly. */
struct zmk_widget_splash_status
{
    lv_obj_t *obj;          /* full-screen overlay */
    lv_timer_t *hold_timer; /* one-shot hold before fade-out */
};

int zmk_widget_splash_status_init(struct zmk_widget_splash_status *widget, lv_obj_t *parent);
