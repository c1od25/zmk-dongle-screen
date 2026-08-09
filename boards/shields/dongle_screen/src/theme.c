/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/central.h>

#include "theme.h"

#define THEME_LEVEL_UNKNOWN 0xFF
#define THEME_MAX_REFRESH   8

static uint8_t last_levels[2] = {THEME_LEVEL_UNKNOWN, THEME_LEVEL_UNKNOWN};
static lv_color_t accent = THEME_ACCENT_RED;
static bool asleep;

static accent_refresh_cb_t refresh_cbs[THEME_MAX_REFRESH];
static uint8_t refresh_count;
static int fade_var;

lv_color_t theme_accent_color(void)
{
    return accent;
}

void theme_register_refresh(accent_refresh_cb_t cb)
{
    if (refresh_count < THEME_MAX_REFRESH)
    {
        refresh_cbs[refresh_count++] = cb;
    }
}

static void theme_fire_refresh(void)
{
    for (uint8_t i = 0; i < refresh_count; i++)
    {
        refresh_cbs[i]();
    }
}

static bool theme_compute_asleep(void)
{
    return !((last_levels[0] >= 1) || (last_levels[1] >= 1));
}

static void theme_fade_exec_cb(void *var, int32_t v)
{
    /* lv_color_mix: mix==0 → c2 (cyan), mix==255 → c1 (red).
     * The animation value v therefore goes 255→0 for a fade to cyan and
     * 0→255 for a fade back to red (see theme_start_fade). */
    accent = lv_color_mix(THEME_ACCENT_RED, THEME_ACCENT_CYAN, (uint8_t)v);
    theme_fire_refresh();
}

static void theme_start_fade(bool to_cyan)
{
    lv_anim_delete(&fade_var, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &fade_var);
    lv_anim_set_exec_cb(&a, theme_fade_exec_cb);
    lv_anim_set_values(&a, to_cyan ? 255 : 0, to_cyan ? 0 : 255);
    lv_anim_set_duration(&a, THEME_FADE_MS);
    lv_anim_start(&a);
}

static void theme_work_cb(struct k_work *work)
{
    bool now_asleep = theme_compute_asleep();
    if (now_asleep != asleep)
    {
        asleep = now_asleep;
        theme_start_fade(asleep);
    }
}
K_WORK_DEFINE(theme_work, theme_work_cb);

static int theme_listener_cb(const zmk_event_t *eh)
{
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    if (ev != NULL && ev->source < ARRAY_SIZE(last_levels))
    {
        last_levels[ev->source] = ev->state_of_charge;
    }

    if (zmk_display_is_initialized())
    {
        k_work_submit_to_queue(zmk_display_work_q(), &theme_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(theme, theme_listener_cb);
ZMK_SUBSCRIPTION(theme, zmk_peripheral_battery_state_changed);

static void theme_poll_cb(lv_timer_t *timer)
{
    uint8_t level = 0;
    bool changed = false;

    for (uint8_t i = 0; i < ARRAY_SIZE(last_levels); i++)
    {
        if (zmk_split_central_get_peripheral_battery_level(i, &level) != 0)
        {
            continue;
        }

        if (level > 0 || last_levels[i] != THEME_LEVEL_UNKNOWN)
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
        k_work_submit_to_queue(zmk_display_work_q(), &theme_work);
    }
}

void theme_init(void)
{
    asleep = theme_compute_asleep();
    accent = asleep ? THEME_ACCENT_CYAN : THEME_ACCENT_RED;
    lv_timer_create(theme_poll_cb, 1000, NULL);
}
