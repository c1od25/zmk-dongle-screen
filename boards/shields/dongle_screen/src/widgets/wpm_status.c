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
#include <zmk/events/wpm_state_changed.h>

#include "wpm_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
struct wpm_status_state
{
    int wpm;
};

/*
 * Module-static buffer for the WPM value text.
 *
 * Kept at file scope so the buffer lifetime matches lv_label_set_text_static()
 * (which does NOT copy the string). The update path must never allocate
 * dynamic text — it only snprintf's into this buffer.
 */
static char buf[8];

static struct wpm_status_state get_state(const zmk_event_t *_eh)
{
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(_eh);

    return (struct wpm_status_state){
        .wpm = ev ? ev->state : 0};
}

static void set_wpm(struct zmk_widget_wpm_status *widget, struct wpm_status_state state)
{
    snprintf(buf, sizeof(buf), "%u", state.wpm);
    lv_label_set_text_static(widget->wpm_value, buf);
}

static void wpm_status_update_cb(struct wpm_status_state state)
{
    struct zmk_widget_wpm_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        set_wpm(widget, state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_status, struct wpm_status_state,
                            wpm_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_wpm_status, zmk_wpm_state_changed);

int zmk_widget_wpm_status_init(struct zmk_widget_wpm_status *widget, lv_obj_t *parent)
{
    /*
     * Container rect (design doc §6.5):
     *   portrait  (66,226) 108x82  — wpm cell, middle column, bottom row
     *   landscape (60,174) 200x56  — wpm spans middle columns, bottom row
     * The widget self-positions; the screen file must not re-align it.
     */
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
    lv_obj_set_pos(widget->obj, 60, 174);
    lv_obj_set_size(widget->obj, 200, 56);
#else
    lv_obj_set_pos(widget->obj, 66, 226);
    lv_obj_set_size(widget->obj, 108, 82);
#endif

    /*
     * WPM value — bottom-anchored, fg-hi (#ececef).
     *
     * Font note: the design specifies a 26 px value font, but no 26 px font is
     * committed in the fork (only montserrat_20 / montserrat_40 / unscii_8).
     * montserrat_20 is the closest available size; a dedicated 26 px font is a
     * future font task.
     *
     * Icon fallback: the F04C5 speedometer glyph is MISSING in both committed
     * NerdFonts (font audit) — the widget renders text-only "NN wpm"
     * (value + "wpm" suffix) and skips the icon entirely. Documented fallback.
     */
    widget->wpm_value = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->wpm_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(widget->wpm_value, lv_color_hex(0xECECEF), 0);
    lv_obj_align(widget->wpm_value, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_label_set_text_static(widget->wpm_value, "0");

    widget->wpm_suffix = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->wpm_suffix, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(widget->wpm_suffix, lv_color_hex(0x383842), 0);
    lv_obj_set_style_text_letter_space(widget->wpm_suffix, 2, 0);
    lv_label_set_text_static(widget->wpm_suffix, "wpm");
    lv_obj_align_to(widget->wpm_suffix, widget->wpm_value, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

    sys_slist_append(&widgets, &widget->node);

    widget_wpm_status_init();
    return 0;
}

lv_obj_t *zmk_widget_wpm_status_obj(struct zmk_widget_wpm_status *widget)
{
    return widget->obj;
}
