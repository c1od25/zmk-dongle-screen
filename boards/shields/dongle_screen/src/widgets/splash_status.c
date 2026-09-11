/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>

#include "splash_status.h"
#include <theme.h>

#define SPLASH_HOLD_MS 2000
#define SPLASH_FADE_MS 500

/* Fade-out of the whole overlay. var == widget so lv_anim_delete works. */
static void splash_fade_exec_cb(void *var, int32_t v) {
    struct zmk_widget_splash_status *w = var;
    lv_obj_set_style_opa(w->obj, (lv_opa_t)v, LV_PART_MAIN);
}

static void splash_done_cb(lv_anim_t *a) {
    struct zmk_widget_splash_status *widget = lv_anim_get_user_data(a);
    lv_obj_delete(widget->obj);
}

static void splash_hold_cb(lv_timer_t *timer) {
    struct zmk_widget_splash_status *widget = lv_timer_get_user_data(timer);
    widget->hold_timer = NULL;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, widget);
    lv_anim_set_user_data(&a, widget);
    lv_anim_set_exec_cb(&a, splash_fade_exec_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, SPLASH_FADE_MS);
    lv_anim_set_completed_cb(&a, splash_done_cb);
    lv_anim_start(&a);
}

int zmk_widget_splash_status_init(struct zmk_widget_splash_status *widget, lv_obj_t *parent) {
    lv_disp_t *disp = lv_obj_get_display(parent);
    lv_coord_t w = lv_disp_get_hor_res(disp);
    lv_coord_t h = lv_disp_get_ver_res(disp);

    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(widget->obj, w, h);
    lv_obj_set_pos(widget->obj, 0, 0);
    lv_obj_set_style_bg_color(widget->obj, lv_color_hex(THEME_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, LV_PART_MAIN);

    widget->hold_timer = lv_timer_create(splash_hold_cb, SPLASH_HOLD_MS, widget);
    if (widget->hold_timer == NULL) {
        /* Out of LVGL timers: leave the splash static (never fades out) rather
         * than dereferencing NULL. */
        LOG_ERR("splash widget: timer allocation failed");
        return 0;
    }
    lv_timer_set_repeat_count(widget->hold_timer, 1);

    return 0;
}

lv_obj_t *zmk_widget_splash_status_obj(struct zmk_widget_splash_status *widget) {
    return widget->obj;
}
