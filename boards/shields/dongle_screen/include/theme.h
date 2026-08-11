/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

/*
 * Sleep-state accent theme.
 *
 * When there is no key activity for SLEEP_ACTIVITY_TIMEOUT_MS (30s), every
 * accent-red UI element on the screen gradually fades to cyan-blue over
 * THEME_FADE_MS, and fades back to red on the next key press.
 *
 * Sleep is determined by key activity (zmk_keycode_state_changed), NOT by
 * peripheral battery levels: the halves no longer use deep sleep
 * (CONFIG_ZMK_SLEEP=n) so they never drop their BAS battery level to 0.
 *
 * Widgets that render accent-red colors:
 *   - replace their hardcoded red with theme_accent_color()
 *   - register a refresh callback via theme_register_refresh() so the fade
 *     animation can re-apply the interpolated color on every frame
 */

#define THEME_ACCENT_RED  ((lv_color_t)LV_COLOR_MAKE(0xef, 0x4d, 0x43))
#define THEME_ACCENT_CYAN ((lv_color_t)LV_COLOR_MAKE(0x30, 0xc6, 0xd9))
#define THEME_FADE_MS     2000

/* No key activity for this long → sleep mode (accent fades to cyan). */
#define SLEEP_ACTIVITY_TIMEOUT_MS 10000

typedef void (*accent_refresh_cb_t)(void);

/* Current interpolated accent color (red awake, cyan asleep). */
lv_color_t theme_accent_color(void);

/* Whether the keyboard is currently in sleep mode (no key for 30s). */
bool theme_is_asleep(void);

/* Register a refresh callback invoked on every fade frame (display thread). */
void theme_register_refresh(accent_refresh_cb_t cb);

/* Initialize the theme: sets the initial accent from current sleep state and
 * starts the staleness poll timer. Call from the display screen init. */
void theme_init(void);
