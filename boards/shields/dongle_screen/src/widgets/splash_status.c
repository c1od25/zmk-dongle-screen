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
#include <fonts.h>
#include <theme.h>

#if CONFIG_DONGLE_SCREEN_HORIZONTAL
#define SPLASH_LOGO_Y 82
#define SPLASH_TITLE_Y 148
#define SPLASH_SUB_Y 178
#else
#define SPLASH_LOGO_Y 96
#define SPLASH_TITLE_Y 170
#define SPLASH_SUB_Y 200
#endif

#define SPLASH_HOLD_MS 2000
#define SPLASH_FADE_MS 500

/* Static text buffer: lv_label_set_text_static() does not copy. */
static char splash_sub_buf[24];

static struct zmk_widget_splash_status *splash_widget;

/* Fade-out of the whole overlay. var == widget so lv_anim_delete works. */
static void splash_fade_exec_cb(void *var, int32_t v)
{
    struct zmk_widget_splash_status *w = var;
    lv_obj_set_style_opa(w->obj, (lv_opa_t)v, LV_PART_MAIN);
}

static void splash_done_cb(lv_anim_t *a)
{
    struct zmk_widget_splash_status *widget = lv_anim_get_user_data(a);
    lv_obj_delete(widget->obj);
}

static void splash_hold_cb(lv_timer_t *timer)
{
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

/* Breathing opacity on the logo for a subtle "booting" pulse. */
static void splash_logo_breath_exec_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa(var, (lv_opa_t)v, LV_PART_MAIN);
}

static void splash_start_breath(lv_obj_t *logo)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, logo);
    lv_anim_set_exec_cb(&a, splash_logo_breath_exec_cb);
    lv_anim_set_values(&a, LV_OPA_70, LV_OPA_COVER);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_playback_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

int zmk_widget_splash_status_init(struct zmk_widget_splash_status *widget, lv_obj_t *parent)
{
    lv_disp_t *disp = lv_obj_get_display(parent);
    lv_coord_t w = lv_disp_get_hor_res(disp);
    lv_coord_t h = lv_disp_get_ver_res(disp);

    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(widget->obj, w, h);
    lv_obj_set_pos(widget->obj, 0, 0);
    lv_obj_set_style_bg_color(widget->obj, lv_color_hex(0x0a0a0d), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, LV_PART_MAIN);

    /* ErgoAstra logo glyph (U+EB04 gripper, reused from the rain matrix). */
    lv_obj_t *logo = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(logo, &Gripper_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(logo, theme_accent_color(), LV_PART_MAIN);
    lv_label_set_text_static(logo, "\U0000EB04");
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, SPLASH_LOGO_Y);

    lv_obj_t *title = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(title, &Mono_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xececef), LV_PART_MAIN);
    lv_label_set_text_static(title, "ERGOSTRA");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SPLASH_TITLE_Y);

    lv_obj_t *sub = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(sub, &Mono_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x9a9aa5), LV_PART_MAIN);
    snprintf(splash_sub_buf, sizeof(splash_sub_buf), "INITIALIZING");
    lv_label_set_text_static(sub, splash_sub_buf);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, SPLASH_SUB_Y);

    splash_start_breath(logo);

    widget->hold_timer = lv_timer_create(splash_hold_cb, SPLASH_HOLD_MS, widget);
    lv_timer_set_repeat_count(widget->hold_timer, 1);

    splash_widget = widget;
    return 0;
}

lv_obj_t *zmk_widget_splash_status_obj(struct zmk_widget_splash_status *widget)
{
    return widget->obj;
}
