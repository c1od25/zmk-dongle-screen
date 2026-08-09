/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

/*
 * Sleep-state accent theme.
 *
 * When BOTH split halves are asleep (peripheral battery level < 1 on both
 * slots), every accent-red UI element on the screen gradually fades to
 * cyan-blue over THEME_FADE_MS, and fades back to red on wake.
 *
 * Widgets that render accent-red colors:
 *   - replace their hardcoded red with theme_accent_color()
 *   - register a refresh callback via theme_register_refresh() so the fade
 *     animation can re-apply the interpolated color on every frame
 */

#define THEME_ACCENT_RED  ((lv_color_t)LV_COLOR_MAKE(0xef, 0x4d, 0x43))
#define THEME_ACCENT_CYAN ((lv_color_t)LV_COLOR_MAKE(0x45, 0xb8, 0xc6))
#define THEME_FADE_MS     2000

typedef void (*accent_refresh_cb_t)(void);

/* Current interpolated accent color (red awake, cyan asleep). */
lv_color_t theme_accent_color(void);

/* Register a refresh callback invoked on every fade frame (display thread). */
void theme_register_refresh(accent_refresh_cb_t cb);

/* Initialize the theme: sets the initial accent from current sleep state and
 * starts the staleness poll timer. Call from the display screen init. */
void theme_init(void);
