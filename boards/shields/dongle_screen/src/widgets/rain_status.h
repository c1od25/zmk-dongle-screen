/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

/* Code-rain idle background animation in the showkey cell.
 *
 * Fades in (2s) after 5s without a key press, using its own idle timer —
 * deliberately decoupled from the theme's 30s sleep accent fade, so code
 * rain appears while the accent is still red. Fades back out fast (250ms)
 * on the next key press so showkey takes over. Drives a small RGB565 canvas
 * at 20fps with randomly spawned drops (random column / speed / timing — no
 * periodic schedule or loop). */
struct zmk_widget_rain_status {
    sys_snode_t node;
    lv_obj_t *obj;            /* the canvas itself */
    lv_timer_t *timer;        /* 50ms frame timer (paused while hidden) */
    lv_timer_t *gate_timer;   /* 500ms fade-in gate poll (always running) */
    bool visible;             /* animation active (fading in/out or running) */
    bool key_pressed;         /* a key is currently held */
    int64_t last_activity_ms; /* k_uptime_get() of the last key press */
};

int zmk_widget_rain_status_init(struct zmk_widget_rain_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_rain_status_obj(struct zmk_widget_rain_status *widget);
