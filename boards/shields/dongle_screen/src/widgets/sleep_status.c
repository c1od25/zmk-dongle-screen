/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#include "sleep_status.h"
#include <fonts.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* Sleep moon nf-md-brightness_3 (U+F00DC). Verified UTF-8: F3 B0 83 9C. */
#define SLEEP_MOON "\U000F00DC"

#define SLEEP_COLOR 0xef4d43

/* Per-source last battery level (<1 = peripheral asleep/disconnected). */
static uint8_t last_levels[2] = {0, 0};

struct sleep_status_state
{
    bool asleep;
};

static void set_sleep_symbol(struct zmk_widget_sleep_status *widget, struct sleep_status_state state)
{
    if (state.asleep)
    {
        lv_obj_remove_flag(widget->label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(widget->label, lv_color_hex(SLEEP_COLOR), LV_PART_MAIN);
    }
    else
    {
        lv_obj_add_flag(widget->label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void sleep_status_update_cb(struct sleep_status_state state)
{
    struct zmk_widget_sleep_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_sleep_symbol(widget, state); }
}

static struct sleep_status_state sleep_status_get_state(const zmk_event_t *eh)
{
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    if (ev != NULL && ev->source < ARRAY_SIZE(last_levels))
    {
        last_levels[ev->source] = ev->state_of_charge;
    }

    return (struct sleep_status_state){
        .asleep = (last_levels[0] < 1 && last_levels[1] < 1),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_sleep_status, struct sleep_status_state,
                            sleep_status_update_cb, sleep_status_get_state)

ZMK_SUBSCRIPTION(widget_sleep_status, zmk_peripheral_battery_state_changed);

int zmk_widget_sleep_status_init(struct zmk_widget_sleep_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(widget->obj, 12, 22);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(widget->obj, 0, LV_PART_MAIN);

    widget->label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label, &NerdFonts_Regular_20, LV_PART_MAIN);
    lv_label_set_text_static(widget->label, SLEEP_MOON);
    lv_obj_set_style_text_color(widget->label, lv_color_hex(SLEEP_COLOR), LV_PART_MAIN);
    lv_obj_align(widget->label, LV_ALIGN_TOP_LEFT, 0, 1);
    lv_obj_add_flag(widget->label, LV_OBJ_FLAG_HIDDEN);

    sys_slist_append(&widgets, &widget->node);

    widget_sleep_status_init();

    return 0;
}

lv_obj_t *zmk_widget_sleep_status_obj(struct zmk_widget_sleep_status *widget)
{
    return widget->obj;
}
