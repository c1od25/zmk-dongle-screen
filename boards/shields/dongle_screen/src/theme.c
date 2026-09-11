/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

#include "theme.h"

/* Registered accent-refresh callbacks. Bumped above the current 7 users so a
 * new widget does not silently overflow; a full table is a hard error in
 * assert-enabled builds and logs an error otherwise. */
#define THEME_MAX_REFRESH 16

static int64_t last_activity_ms;
static lv_color_t accent = THEME_ACCENT_RED;
static bool asleep;

static accent_refresh_cb_t refresh_cbs[THEME_MAX_REFRESH];
static uint8_t refresh_count;
static int fade_var;

static void theme_start_fade(bool to_cyan);

lv_color_t theme_accent_color(void)
{
    return accent;
}

bool theme_is_asleep(void)
{
    return asleep;
}

void theme_register_refresh(accent_refresh_cb_t cb)
{
    __ASSERT(refresh_count < THEME_MAX_REFRESH,
             "theme refresh table full (%d entries)", THEME_MAX_REFRESH);
    if (refresh_count >= THEME_MAX_REFRESH)
    {
        LOG_ERR("theme refresh table full (%d entries); callback dropped", THEME_MAX_REFRESH);
        return;
    }
    refresh_cbs[refresh_count++] = cb;
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
    return (k_uptime_get() - last_activity_ms) > SLEEP_ACTIVITY_TIMEOUT_MS;
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
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);

    if (ev != NULL && ev->state)
    {
        last_activity_ms = k_uptime_get();
        if (zmk_display_is_initialized())
        {
            k_work_submit_to_queue(zmk_display_work_q(), &theme_work);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(theme, theme_listener_cb);
ZMK_SUBSCRIPTION(theme, zmk_keycode_state_changed);

static void theme_poll_cb(lv_timer_t *timer)
{
    if (theme_compute_asleep() != asleep)
    {
        k_work_submit_to_queue(zmk_display_work_q(), &theme_work);
    }
}

void theme_init(void)
{
    last_activity_ms = k_uptime_get();
    asleep = theme_compute_asleep();
    accent = asleep ? THEME_ACCENT_CYAN : THEME_ACCENT_RED;
    lv_timer_create(theme_poll_cb, 1000, NULL);
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
