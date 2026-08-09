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

/* Work/sleep icons (NerdFonts_Regular_28 — both glyphs render 17x17 @28px, so
 * they match the visual height of the BT/USB icons in the top bar):
 *   nf-md-wifi (U+F0A3B). UTF-8: F3 B0 A8 BB
 *   nf-md-leaf (U+F032A). UTF-8: F3 B0 8C AA
 */
#define SLEEP_WIFI "\U000F0A3B"
#define SLEEP_LEAF "\U000F032A"

/* Icon colors — same palette as output_status.c:
 *   working/sleeping = red (COLOR_RED 0xef4d43)
 *   never connected  = dim (COLOR_FG_FAINT 0x383842, like the BT icon)
 */
#define SLEEP_RED 0xef4d43
#define SLEEP_DIM 0x383842

/* 0xFF = unknown (never connected). */
#define SLEEP_LEVEL_UNKNOWN 0xFF

enum sleep_mode
{
    SLEEP_MODE_WORKING,
    SLEEP_MODE_SLEEPING,
    SLEEP_MODE_DISCONNECTED,
};

/* Per-source last battery level (<1 = peripheral asleep/disconnected). */
static uint8_t last_levels[2] = {SLEEP_LEVEL_UNKNOWN, SLEEP_LEVEL_UNKNOWN};

/* Per-source "has been seen alive at least once". The central's cached battery
 * level is {0,0} until a half first connects, so a 0 is only meaningful after
 * the half has actually been observed. */
static bool seen_connected[2] = {false, false};

struct sleep_status_state
{
    enum sleep_mode mode;
};

static void set_sleep_symbol(struct zmk_widget_sleep_status *widget, struct sleep_status_state state)
{
    /* Icon is ALWAYS visible — swap glyph/color instead of hiding. */
    lv_obj_remove_flag(widget->label, LV_OBJ_FLAG_HIDDEN);

    if (state.mode == SLEEP_MODE_WORKING)
    {
        lv_label_set_text_static(widget->label, SLEEP_WIFI);
        lv_obj_set_style_text_color(widget->label, lv_color_hex(SLEEP_RED), LV_PART_MAIN);
    }
    else
    {
        lv_label_set_text_static(widget->label, SLEEP_LEAF);
        lv_obj_set_style_text_color(
            widget->label,
            lv_color_hex(state.mode == SLEEP_MODE_SLEEPING ? SLEEP_RED : SLEEP_DIM),
            LV_PART_MAIN);
    }
}

static void sleep_status_update_cb(struct sleep_status_state state)
{
    struct zmk_widget_sleep_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_sleep_symbol(widget, state); }
}

static enum sleep_mode sleep_status_compute_mode(void)
{
    bool both_seen = seen_connected[0] && seen_connected[1];
    bool any_awake = (last_levels[0] >= 1) || (last_levels[1] >= 1);

    if (any_awake)
    {
        return SLEEP_MODE_WORKING;
    }

    if (both_seen)
    {
        return SLEEP_MODE_SLEEPING;
    }

    return SLEEP_MODE_DISCONNECTED;
}

static struct sleep_status_state sleep_status_get_state(const zmk_event_t *eh)
{
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    if (ev != NULL && ev->source < ARRAY_SIZE(last_levels))
    {
        last_levels[ev->source] = ev->state_of_charge;
        if (ev->state_of_charge > 0)
        {
            seen_connected[ev->source] = true;
        }
    }

    return (struct sleep_status_state){
        .mode = sleep_status_compute_mode(),
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
                if (level > 0)
                {
                    seen_connected[i] = true;
                }
                changed = true;
            }
        }
    }

    if (changed)
    {
        sleep_status_update_cb((struct sleep_status_state){
            .mode = sleep_status_compute_mode(),
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
    lv_obj_set_size(widget->obj, 18, 22);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(widget->obj, 0, LV_PART_MAIN);

    widget->label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label, &NerdFonts_Regular_28, LV_PART_MAIN);
    lv_label_set_text_static(widget->label, SLEEP_WIFI);
    lv_obj_set_style_text_color(widget->label, lv_color_hex(SLEEP_RED), LV_PART_MAIN);
    lv_obj_align(widget->label, LV_ALIGN_TOP_LEFT, 0, 2);

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
