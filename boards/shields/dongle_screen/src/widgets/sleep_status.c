/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>

#include "sleep_status.h"
#include <fonts.h>
#include <theme.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* Work/sleep icons (NerdFonts_Regular_28 — both glyphs render 17px wide @28px,
 * so they match the visual height of the BT/USB icons in the top bar):
 *   nf-md-wifi (U+F05A9). UTF-8: F3 B0 96 A9
 *   nf-md-leaf (U+F032A). UTF-8: F3 B0 8C AA
 */
#define SLEEP_WIFI "\U000F05A9"
#define SLEEP_LEAF "\U000F032A"

/* Sleep state is owned by theme.c (30s no-key-activity timeout); this widget
 * only mirrors it so the icon and the accent fade stay in sync. */
static bool last_asleep;

static void set_sleep_symbol(struct zmk_widget_sleep_status *widget, bool asleep)
{
    lv_label_set_text_static(widget->label, asleep ? SLEEP_LEAF : SLEEP_WIFI);
    lv_obj_set_style_text_color(widget->label, theme_accent_color(), LV_PART_MAIN);
}

static void sleep_status_refresh(void)
{
    struct zmk_widget_sleep_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        lv_obj_set_style_text_color(widget->label, theme_accent_color(), LV_PART_MAIN);
    }
}

static void sleep_status_poll_cb(lv_timer_t *timer)
{
    bool asleep = theme_is_asleep();
    if (asleep == last_asleep)
    {
        return;
    }

    last_asleep = asleep;
    struct zmk_widget_sleep_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_sleep_symbol(widget, asleep); }
}

int zmk_widget_sleep_status_init(struct zmk_widget_sleep_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(widget->obj, 18, 22);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(widget->obj, 0, LV_PART_MAIN);

    widget->label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label, &NerdFonts_Regular_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->label, theme_accent_color(), LV_PART_MAIN);
    lv_obj_align(widget->label, LV_ALIGN_TOP_LEFT, 0, 2);

    sys_slist_append(&widgets, &widget->node);

    theme_register_refresh(sleep_status_refresh);

    last_asleep = theme_is_asleep();
    set_sleep_symbol(widget, last_asleep);

    widget->poll_timer = lv_timer_create(sleep_status_poll_cb, 1000, widget);

    return 0;
}

lv_obj_t *zmk_widget_sleep_status_obj(struct zmk_widget_sleep_status *widget)
{
    return widget->obj;
}
