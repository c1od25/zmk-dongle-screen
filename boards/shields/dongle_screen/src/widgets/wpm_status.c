/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <lvgl.h>

#include "wpm_status.h"

/* Block-character scanner (user-approved kr_4_12 rhythm). 15 discrete square
 * blocks, head advances without stopping, 6-level exponential trail behind it,
 * 4-frame go-back gap and 12-frame cycle gap of all-inactive dots. No ZMK
 * events needed — a 60ms lv_timer drives a frame counter on the display thread.
 */
#define WPM_FRAME_MS      60
#define WPM_CYCLE         56 /* 15 fwd + 5 out + 4 gap + 15 back + 5 out + 12 gap */

#define WPM_BLOCK_SIZE    6
#define WPM_DOT_SIZE      3
#define WPM_BLOCK_GAP     1

#if CONFIG_DONGLE_SCREEN_HORIZONTAL
#define WPM_CELL_X        60
#define WPM_CELL_Y        174
#define WPM_CELL_W        200
#define WPM_CELL_H        56
#define WPM_BLOCKS_X      48 /* (200 - (15*6 + 14*1))/2 */
#define WPM_BLOCKS_Y      25 /* (56 - 6)/2 */
#else
#define WPM_CELL_X        66
#define WPM_CELL_Y        226
#define WPM_CELL_W        108
#define WPM_CELL_H        82
#define WPM_BLOCKS_X      2  /* (108 - 104)/2 */
#define WPM_BLOCKS_Y      38 /* (82 - 6)/2 */
#endif

/* Trail alphas (head then 5 exponential-decay steps); inactive dot is a
 * mid-strength mix. RGB is lerped toward the panel bg 0x0a0a0d because LVGL
 * lv_obj backgrounds are opaque (no per-block alpha blending). */
#define SCAN_BG_R         10
#define SCAN_BG_G         10
#define SCAN_BG_B         13
#define SCAN_RED_R        0xef
#define SCAN_RED_G        0x4d
#define SCAN_RED_B        0x43

static lv_color_t trail_colors[6];
static lv_color_t inactive_color;
static int frame_idx;
static uint8_t last_state[WPM_BLOCK_N];

static lv_color_t mix_toward_bg(uint8_t r, uint8_t g, uint8_t b, float alpha)
{
    uint8_t mr = (uint8_t)(SCAN_BG_R + (r - SCAN_BG_R) * alpha);
    uint8_t mg = (uint8_t)(SCAN_BG_G + (g - SCAN_BG_G) * alpha);
    uint8_t mb = (uint8_t)(SCAN_BG_B + (b - SCAN_BG_B) * alpha);
    return lv_color_make(mr, mg, mb);
}

static void scan_timer_cb(lv_timer_t *t)
{
    struct zmk_widget_wpm_status *widget = lv_timer_get_user_data(t);
    int f = frame_idx++;

    /* State machine: head position + direction, or an all-inactive gap. */
    int head, dir;
    bool all_inactive;

    if (f < 15) {
        head = f;
        dir = 1;
        all_inactive = false;
    } else if (f < 20) {
        head = f;               /* 15..19: head exits the right edge */
        dir = 1;
        all_inactive = false;
    } else if (f < 24) {
        all_inactive = true;    /* go-back gap (4 frames) */
        head = 0;
        dir = 1;
    } else if (f < 39) {
        head = 14 - (f - 24);   /* 14..0 */
        dir = -1;
        all_inactive = false;
    } else if (f < 44) {
        head = -1 - (f - 39);   /* -1..-5: head exits the left edge */
        dir = -1;
        all_inactive = false;
    } else {
        all_inactive = true;    /* cycle gap (12 frames) */
        head = 0;
        dir = 1;
    }
    if (frame_idx >= WPM_CYCLE) {
        frame_idx = 0;
    }

    for (int i = 0; i < WPM_BLOCK_N; i++) {
        /* state: 0 = inactive dot, 1..6 = head(1) + trail levels(2..6) */
        int state = 0;
        if (!all_inactive) {
            int k = (head - i) * dir;
            if (k >= 0 && k < 6) {
                state = k + 1;
            }
        }
        if (state == last_state[i]) {
            continue;
        }
        last_state[i] = state;
        lv_obj_t *b = widget->blocks[i];
        if (state == 0) {
            lv_obj_set_size(b, WPM_DOT_SIZE, WPM_DOT_SIZE);
            lv_obj_set_pos(b, WPM_BLOCKS_X + i * (WPM_BLOCK_SIZE + WPM_BLOCK_GAP) + 1,
                           WPM_BLOCKS_Y + 1);
            lv_obj_set_style_bg_color(b, inactive_color, LV_PART_MAIN);
        } else {
            lv_obj_set_size(b, WPM_BLOCK_SIZE, WPM_BLOCK_SIZE);
            lv_obj_set_pos(b, WPM_BLOCKS_X + i * (WPM_BLOCK_SIZE + WPM_BLOCK_GAP),
                           WPM_BLOCKS_Y);
            lv_obj_set_style_bg_color(b, trail_colors[state - 1], LV_PART_MAIN);
        }
    }
}

int zmk_widget_wpm_status_init(struct zmk_widget_wpm_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(widget->obj, WPM_CELL_X, WPM_CELL_Y);
    lv_obj_set_size(widget->obj, WPM_CELL_W, WPM_CELL_H);

    /* Precompute trail colors: mix(bg, red, alpha), alphas [1.0 0.9 0.65 0.42 0.28 0.18]. */
    static const float alphas[6] = {1.0f, 0.9f, 0.65f, 0.42f, 0.28f, 0.18f};
    for (int k = 0; k < 6; k++) {
        trail_colors[k] = mix_toward_bg(SCAN_RED_R, SCAN_RED_G, SCAN_RED_B, alphas[k]);
    }
    inactive_color = mix_toward_bg(SCAN_RED_R, SCAN_RED_G, SCAN_RED_B, 0.6f);

    for (int i = 0; i < WPM_BLOCK_N; i++) {
        lv_obj_t *b = lv_obj_create(widget->obj);
        lv_obj_set_size(b, WPM_DOT_SIZE, WPM_DOT_SIZE);
        lv_obj_set_pos(b, WPM_BLOCKS_X + i * (WPM_BLOCK_SIZE + WPM_BLOCK_GAP) + 1,
                       WPM_BLOCKS_Y + 1);
        lv_obj_set_style_bg_color(b, inactive_color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 0, LV_PART_MAIN); /* sharp corners */
        lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        widget->blocks[i] = b;
        last_state[i] = 0xFF; /* force first update */
    }

    frame_idx = 0;
    lv_timer_create(scan_timer_cb, WPM_FRAME_MS, widget);

    return 0;
}

lv_obj_t *zmk_widget_wpm_status_obj(struct zmk_widget_wpm_status *widget)
{
    return widget->obj;
}
