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
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#include <dt-bindings/zmk/hid_indicators.h>
#include <zmk/usb.h>
#include <zmk/endpoints.h>

#include "output_status.h"
#include <fonts.h>
#include <theme.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct output_status_state
{
    struct zmk_endpoint_instance selected_endpoint;
    bool usb_is_hid_ready;
    bool caps_lock;
};

static struct output_status_state get_state(const zmk_event_t *eh)
{
    bool caps_lock = false;
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);
    if (ev != NULL)
    {
        caps_lock = (ev->indicators & HID_INDICATOR_CAPS_LOCK) != 0;
    }
    else
    {
        caps_lock = (zmk_hid_indicators_get_current_profile() & HID_INDICATOR_CAPS_LOCK) != 0;
    }

    return (struct output_status_state){
        .selected_endpoint = zmk_endpoint_get_selected(), // 0 = USB , 1 = BLE
        .usb_is_hid_ready = zmk_usb_is_hid_ready(),       // 0 = not ready, 1 = ready
        .caps_lock = caps_lock};
}

#define COLOR_FG_MID ((lv_color_t)LV_COLOR_MAKE(0xa9, 0xb1, 0xd6))
#define COLOR_FG_FAINT ((lv_color_t)LV_COLOR_MAKE(0x73, 0x7a, 0xa2))

/* Caps lock icon — nf-md-caps_lock (U+F0A9B), top bar, left of the wifi icon.
 * Active = accent color, inactive = faint gray (design tri-state). */
#define CAPS_LOCK_ICON "\U000F0A9B"

static void set_caps_symbol(struct zmk_widget_output_status *widget, bool caps_lock)
{
    lv_obj_set_style_text_color(widget->caps_label,
                                caps_lock ? theme_accent_color() : COLOR_FG_FAINT, LV_PART_MAIN);
}

static void set_status_symbol(struct zmk_widget_output_status *widget, struct output_status_state state)
{
    if (state.usb_is_hid_ready && state.selected_endpoint.transport == ZMK_TRANSPORT_USB)
    {
        lv_obj_set_style_text_color(widget->usb_label, theme_accent_color(), LV_PART_MAIN);
    }
    else if (state.usb_is_hid_ready)
    {
        lv_obj_set_style_text_color(widget->usb_label, COLOR_FG_MID, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_text_color(widget->usb_label, COLOR_FG_FAINT, LV_PART_MAIN);
    }
}

static void output_status_update_cb(struct output_status_state state)
{
    struct zmk_widget_output_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        set_status_symbol(widget, state);
        set_caps_symbol(widget, state.caps_lock);
    }
}

static void output_status_refresh(void)
{
    output_status_update_cb(get_state(NULL));
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(widget_output_status, zmk_hid_indicators_changed);

// output_status.c
int zmk_widget_output_status_init(struct zmk_widget_output_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
    lv_obj_set_size(widget->obj, 320, 22);
#else
    lv_obj_set_size(widget->obj, 240, 22);
#endif
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(widget->obj, 0, LV_PART_MAIN);

    widget->usb_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->usb_label, &NerdFonts_Regular_20, LV_PART_MAIN);
    lv_label_set_text_static(widget->usb_label, "\U000F11F0"); /* U+F11F0 nf-md-usb_port */
    lv_obj_set_style_text_align(widget->usb_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_align(widget->usb_label, LV_ALIGN_TOP_LEFT, 12, 1);

    /* Caps lock icon — 28px Nerd Font (17px wide), right-aligned just left
     * of the wifi icon. The wifi icon sits 30px from the top bar right edge
     * in both orientations; the caps glyph adds a 7px gap → 37px offset. */
    widget->caps_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->caps_label, &NerdFonts_Regular_28, LV_PART_MAIN);
    lv_label_set_text_static(widget->caps_label, CAPS_LOCK_ICON);
    lv_obj_set_style_text_align(widget->caps_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(widget->caps_label, LV_ALIGN_TOP_RIGHT, -37, 2);
    set_caps_symbol(widget, false);

    sys_slist_append(&widgets, &widget->node);

    theme_register_refresh(output_status_refresh);

    widget_output_status_init();
    return 0;
}

lv_obj_t *zmk_widget_output_status_obj(struct zmk_widget_output_status *widget)
{
    return widget->obj;
}
