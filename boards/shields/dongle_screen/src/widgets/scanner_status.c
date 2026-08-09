/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <lvgl.h>

#include "scanner_status.h"
#include <theme.h>

/* Block-character scanning loading animation (kr_4_12 rhythm scaled to 10
 * blocks). The head always advances (never stops at ends), a 6-level
 * exponential trail follows behind it, with a 4-frame go-back gap and a
 * 12-frame cycle gap of all-inactive dots. A 60ms lv_timer drives a frame
 * counter on the display thread (no ZMK events, zero dynamic alloc).
 */
#define SCANNER_FRAME_MS  60
#define SCANNER_CYCLE     46 /* 10 fwd + 5 out + 4 gap + 10 back + 5 out + 12 gap */

#define SCANNER_BLOCK_SIZE 10
#define SCANNER_DOT_SIZE   4
#define SCANNER_GAP        0
#define SCANNER_DOT_OFF    ((SCANNER_BLOCK_SIZE - SCANNER_DOT_SIZE) / 2) /* 3 */

#if CONFIG_DONGLE_SCREEN_HORIZONTAL
#define SCANNER_CELL_X      60
#define SCANNER_CELL_Y      174
#define SCANNER_CELL_W      200
#define SCANNER_CELL_H      56
#define SCANNER_BLOCKS_X    50 /* (200 - (10*10 + 9*0))/2 */
#define SCANNER_BLOCKS_Y    23 /* (56 - 10)/2 */
#else
#define SCANNER_CELL_X      66
#define SCANNER_CELL_Y      226
#define SCANNER_CELL_W      108
#define SCANNER_CELL_H      82
#define SCANNER_BLOCKS_X    4  /* (108 - 100)/2 */
#define SCANNER_BLOCKS_Y    36 /* (82 - 10)/2 */
#endif

/* Trail alphas (head then 5 exponential-decay steps); inactive dot is a
 * mid-strength mix. RGB is lerped toward the panel bg 0x0a0a0d because LVGL
 * lv_obj backgrounds are opaque (no per-block alpha blending). */
#define SCAN_BG_R 10
#define SCAN_BG_G 10
#define SCAN_BG_B 13

static const float trail_alphas[6] = {1.0f, 0.9f, 0.65f, 0.42f, 0.28f, 0.18f};

static lv_color_t trail_colors[6];
static lv_color_t inactive_color;
static int frame_idx;
static uint8_t last_state[SCANNER_BLOCK_N];

static lv_color_t mix_toward_bg(uint8_t r, uint8_t g, uint8_t b, float alpha)
{
    uint8_t mr = (uint8_t)(SCAN_BG_R + (r - SCAN_BG_R) * alpha);
    uint8_t mg = (uint8_t)(SCAN_BG_G + (g - SCAN_BG_G) * alpha);
    uint8_t mb = (uint8_t)(SCAN_BG_B + (b - SCAN_BG_B) * alpha);
    return lv_color_make(mr, mg, mb);
}

static void scanner_recompute_colors(void)
{
    lv_color_t accent = theme_accent_color();
    uint32_t rgb = lv_color_to_32(accent);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;

    for (int k = 0; k < 6; k++)
    {
        trail_colors[k] = mix_toward_bg(r, g, b, trail_alphas[k]);
    }
    inactive_color = mix_toward_bg(r, g, b, 0.6f);

    for (int i = 0; i < SCANNER_BLOCK_N; i++)
    {
        last_state[i] = 0xFF; /* force repaint on next frame */
    }
}

static void scanner_status_refresh(void)
{
    scanner_recompute_colors();
}

static void scan_timer_cb(lv_timer_t *t)
{
    struct zmk_widget_scanner_status *widget = lv_timer_get_user_data(t);
    int f = frame_idx++;

    /* State machine: head position + direction, or an all-inactive gap. */
    int head, dir;
    bool all_inactive;

    if (f < 10) {
        head = f;
        dir = 1;
        all_inactive = false;
    } else if (f < 15) {
        head = f;               /* 10..14: head exits the right edge */
        dir = 1;
        all_inactive = false;
    } else if (f < 19) {
        all_inactive = true;    /* go-back gap (4 frames) */
        head = 0;
        dir = 1;
    } else if (f < 29) {
        head = 9 - (f - 19);    /* 9..0 */
        dir = -1;
        all_inactive = false;
    } else if (f < 34) {
        head = -1 - (f - 29);   /* -1..-5: head exits the left edge */
        dir = -1;
        all_inactive = false;
    } else {
        all_inactive = true;    /* cycle gap (12 frames) */
        head = 0;
        dir = 1;
    }
    if (frame_idx >= SCANNER_CYCLE) {
        frame_idx = 0;
    }

    for (int i = 0; i < SCANNER_BLOCK_N; i++) {
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
            /* Inactive: small dot, centered (SCANNER_DOT_OFF in the slot). */
            lv_obj_set_size(b, SCANNER_DOT_SIZE, SCANNER_DOT_SIZE);
            lv_obj_set_pos(b, SCANNER_BLOCKS_X + i * (SCANNER_BLOCK_SIZE + SCANNER_GAP) + SCANNER_DOT_OFF,
                           SCANNER_BLOCKS_Y + SCANNER_DOT_OFF);
            lv_obj_set_style_bg_color(b, inactive_color, LV_PART_MAIN);
        } else {
            lv_obj_set_size(b, SCANNER_BLOCK_SIZE, SCANNER_BLOCK_SIZE);
            lv_obj_set_pos(b, SCANNER_BLOCKS_X + i * (SCANNER_BLOCK_SIZE + SCANNER_GAP),
                           SCANNER_BLOCKS_Y);
            lv_obj_set_style_bg_color(b, trail_colors[state - 1], LV_PART_MAIN);
        }
    }
}

int zmk_widget_scanner_status_init(struct zmk_widget_scanner_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(widget->obj, SCANNER_CELL_X, SCANNER_CELL_Y);
    lv_obj_set_size(widget->obj, SCANNER_CELL_W, SCANNER_CELL_H);

    /* Precompute trail colors from the current theme accent. */
    scanner_recompute_colors();

    for (int i = 0; i < SCANNER_BLOCK_N; i++) {
        lv_obj_t *b = lv_obj_create(widget->obj);
        lv_obj_set_size(b, SCANNER_DOT_SIZE, SCANNER_DOT_SIZE);
        lv_obj_set_pos(b, SCANNER_BLOCKS_X + i * (SCANNER_BLOCK_SIZE + SCANNER_GAP) + SCANNER_DOT_OFF,
                       SCANNER_BLOCKS_Y + SCANNER_DOT_OFF);
        lv_obj_set_style_bg_color(b, inactive_color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 0, LV_PART_MAIN); /* sharp corners */
        lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        widget->blocks[i] = b;
        last_state[i] = 0xFF; /* force first update */
    }

    theme_register_refresh(scanner_status_refresh);

    frame_idx = 0;
    lv_timer_create(scan_timer_cb, SCANNER_FRAME_MS, widget);

    return 0;
}

lv_obj_t *zmk_widget_scanner_status_obj(struct zmk_widget_scanner_status *widget)
{
    return widget->obj;
}
