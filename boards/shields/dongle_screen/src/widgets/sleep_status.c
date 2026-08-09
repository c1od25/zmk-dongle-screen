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
#include <zmk/split/central.h>

#include "sleep_status.h"
#include <fonts.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* Sleep moon nf-md-brightness_2 (U+F00DB). UTF-8: F3 B0 83 9B.
 * 12x17 @ ofs_y=-1 — box metrics match the BT icon (U+F293) exactly, so the
 * two top-bar icons render at the same size. */
#define SLEEP_MOON "\U000F00DB"

/* Moon colors: gray = awake (any half connected), red = both halves asleep.
 * Gray matches COLOR_FG_MID (0x9a9aa5) used by the BT/USB icons. */
#define SLEEP_GRAY 0x9a9aa5
#define SLEEP_RED 0xef4d43

/* 0xFF = unknown (never connected). Treated as awake so the icon starts GRAY
 * at boot instead of flashing red before the halves report in. */
#define SLEEP_LEVEL_UNKNOWN 0xFF

/* Per-source last battery level (<1 = peripheral asleep/disconnected). */
static uint8_t last_levels[2] = {SLEEP_LEVEL_UNKNOWN, SLEEP_LEVEL_UNKNOWN};

struct sleep_status_state
{
    bool asleep;
};

static void set_sleep_symbol(struct zmk_widget_sleep_status *widget, struct sleep_status_state state)
{
    /* Icon is ALWAYS visible — recolor instead of hiding. */
    lv_obj_remove_flag(widget->label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(widget->label, lv_color_hex(state.asleep ? SLEEP_RED : SLEEP_GRAY),
                                LV_PART_MAIN);
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

static void sleep_status_poll_cb(lv_timer_t *timer)
{
    uint8_t level = 0;
    bool changed = false;

    for (uint8_t i = 0; i < ARRAY_SIZE(last_levels); i++)
    {
        if (zmk_split_central_get_peripheral_battery_level(i, &level) != 0)
        {
            continue;
        }

        /* The central cache is {0,0} until a half first connects, so a cached 0
         * is only meaningful once this source has been seen alive. */
        if (level > 0 || last_levels[i] != SLEEP_LEVEL_UNKNOWN)
        {
            if (last_levels[i] != level)
            {
                last_levels[i] = level;
                changed = true;
            }
        }
    }

    if (changed)
    {
        sleep_status_update_cb((struct sleep_status_state){
            .asleep = (last_levels[0] < 1 && last_levels[1] < 1),
        });
    }
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
    lv_obj_set_style_text_color(widget->label, lv_color_hex(SLEEP_GRAY), LV_PART_MAIN);
    lv_obj_align(widget->label, LV_ALIGN_TOP_LEFT, 0, 1);

    sys_slist_append(&widgets, &widget->node);

    /* Poll the central's cached peripheral battery levels every second so a
     * dropped disconnect event can't leave the sleep state stale. LVGL timers
     * run inside lv_timer_handler() on the display thread, so they are
     * thread-safe with LVGL style updates. */
    widget->poll_timer = lv_timer_create(sleep_status_poll_cb, 1000, widget);

    widget_sleep_status_init();

    return 0;
}

lv_obj_t *zmk_widget_sleep_status_obj(struct zmk_widget_sleep_status *widget)
{
    return widget->obj;
}
