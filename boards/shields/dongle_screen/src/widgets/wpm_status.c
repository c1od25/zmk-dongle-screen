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
#include <fonts.h>

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
     * WPM group (speedo icon + 20px gap + value), bottom-aligned in the cell.
     * The speedometer icon (U+F04C5, NerdFonts_Speedo_40 — same 40px as the mod
     * icons) sits LEFT of the value. OUT_LEFT_MID resolves against the value
     * label's actual left edge, and that label's width already covers all 3
     * digits ("110" @ Mono_28 ~50px), so a 20px gap can never be overlapped.
     * Value is right-shifted 22px so the 3-digit group centers in the 108px
     * cell (group ~94px: icon 24 + 20 + value 50, margins 7/7).
     */
    widget->wpm_value = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->wpm_value, &Mono_28, 0);
    lv_obj_set_style_text_color(widget->wpm_value, lv_color_hex(0xc0caf5), 0);
    lv_obj_align(widget->wpm_value, LV_ALIGN_BOTTOM_MID, 22, -3);
    lv_label_set_text_static(widget->wpm_value, "0");

    widget->wpm_icon = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->wpm_icon, &NerdFonts_Speedo_40, 0);
    lv_obj_set_style_text_color(widget->wpm_icon, lv_color_hex(0xa9b1d6), 0);
    lv_obj_set_style_pad_all(widget->wpm_icon, 0, LV_PART_MAIN);
    /* U+F04C5 is above the BMP — needs the 8-digit \U0000XXXX escape, not
     * \uXXXX (a 4-digit \u escape would render a missing-glyph tofu box). */
    lv_label_set_text_static(widget->wpm_icon, "\U000F04C5");
    lv_obj_align_to(widget->wpm_icon, widget->wpm_value, LV_ALIGN_OUT_LEFT_MID, -20, 0);

    sys_slist_append(&widgets, &widget->node);

    widget_wpm_status_init();
    return 0;
}

lv_obj_t *zmk_widget_wpm_status_obj(struct zmk_widget_wpm_status *widget)
{
    return widget->obj;
}
