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

#define THEME_ACCENT_RED ((lv_color_t)LV_COLOR_MAKE(0xf7, 0x76, 0x8e))
#define THEME_ACCENT_CYAN ((lv_color_t)LV_COLOR_MAKE(0x7d, 0xcf, 0xff))
#define THEME_FADE_MS 2000

/*
 * Tokyo Night "night" palette. Every widget must use these instead of
 * hardcoding hex literals, so switching palettes stays a single-file edit
 * (the accent above is the only animated color).
 */
#define THEME_COLOR_BG 0x1a1b26       /* panel background            */
#define THEME_COLOR_BG_ALT 0x16161e   /* inset surfaces (mod keys)   */
#define THEME_COLOR_FG 0xc0caf5       /* primary text                */
#define THEME_COLOR_FG_MID 0xa9b1d6   /* secondary text / idle icons */
#define THEME_COLOR_FG_FAINT 0x737aa2 /* inactive text / icons       */
#define THEME_COLOR_BORDER 0x3b4261   /* separators / outlines       */
#define THEME_COLOR_TRACK 0x292e42    /* bar track behind indicators */

/* Panel background split into channels for the widgets that lerp toward it
 * (opaque RGB565 objects have no per-object alpha). */
#define THEME_COLOR_BG_R ((THEME_COLOR_BG >> 16) & 0xFF)
#define THEME_COLOR_BG_G ((THEME_COLOR_BG >> 8) & 0xFF)
#define THEME_COLOR_BG_B (THEME_COLOR_BG & 0xFF)

/* No key activity for this long → sleep mode (accent fades to cyan). The
 * code-rain widget uses its own independent 10s idle timer, so rain can
 * appear without triggering the whole-UI sleep accent fade. */
#define SLEEP_ACTIVITY_TIMEOUT_MS 30000

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
