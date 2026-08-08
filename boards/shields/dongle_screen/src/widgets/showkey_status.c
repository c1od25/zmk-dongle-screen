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
#include <zmk/events/keycode_state_changed.h>

#include "showkey_status.h"
#include <fonts.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct showkey_status_state
{
    bool pressed;
};

static struct showkey_status_state get_state(const zmk_event_t *_eh)
{
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(_eh);

    return (struct showkey_status_state){
        .pressed = ev && ev->state};
}

static void showkey_flash_timeout(lv_timer_t *timer)
{
    struct zmk_widget_showkey_status *widget = lv_timer_get_user_data(timer);

    lv_obj_set_style_text_color(widget->label, lv_color_hex(0xececef), LV_PART_MAIN);
    lv_timer_delete(timer);
}

static void showkey_status_update_cb(struct showkey_status_state state)
{
    struct zmk_widget_showkey_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        if (state.pressed)
        {
            lv_obj_set_style_text_color(widget->label, lv_color_hex(0xef4d43), LV_PART_MAIN);
            lv_timer_create(showkey_flash_timeout, 300, widget);
        }
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_showkey_status, struct showkey_status_state,
                            showkey_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_showkey_status, zmk_keycode_state_changed);

int zmk_widget_showkey_status_init(struct zmk_widget_showkey_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);

#if CONFIG_DONGLE_SCREEN_HORIZONTAL
    lv_obj_set_size(widget->obj, 200, 56);
    lv_obj_set_pos(widget->obj, 60, 112);
#else
    lv_obj_set_size(widget->obj, 108, 82);
    lv_obj_set_pos(widget->obj, 66, 138);
#endif

    widget->label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label, &NerdFonts_Regular_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->label, lv_color_hex(0xececef), LV_PART_MAIN);
    lv_label_set_text_static(widget->label, "-");
    lv_obj_align(widget->label, LV_ALIGN_CENTER, 0, 0);

    sys_slist_append(&widgets, &widget->node);

    widget_showkey_status_init();
    return 0;
}

lv_obj_t *zmk_widget_showkey_status_obj(struct zmk_widget_showkey_status *widget)
{
    return widget->obj;
}
