/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/random/random.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

#include "rain_status.h"
#include <fonts.h>
#include <theme.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* -------------------------------------------------------------------------
 * Geometry — fixed 4x5 glyph matrix, Mono_20 letters (adv_w 12px) with a
 * 3px column gap (CELL_W = 12 + 3 = 15px) and 3px row gap (CELL_H = 20 + 3).
 *
 *   row0: ⌸ E R G O      row1: a s t r a
 *   row2: e r g o ⌸      row3: A S T R A
 *
 * ⌸ (Nerd Font U+EB04 gripper) sits at the two diagonal corners (top-left
 * and bottom-right) and lights up with its cell like every other glyph.
 * Portrait 4 rows x 23px = 92px exceeds the 82px showkey cell; the canvas
 * starts at Y=141 per user request (5px up from 146).
 * ---------------------------------------------------------------------- */
#define RAIN_COLS 5
#define RAIN_FRAME_MS 50

#define RAIN_FADE_IN_MS 2000
#define RAIN_FADE_OUT_MS 250
#define RAIN_GATE_POLL_MS 500
/* Rain's own idle gate: 5s without a key press. Independent of the theme's
 * 30s sleep accent fade — rain can appear while the UI is still red. */
#define RAIN_IDLE_TIMEOUT_MS 5000
#define RAIN_GRIPPER "\U0000EB04"
/* Gripper glyph (U+EB04, size 22) content is 9px tall — 64% of Mono_20's
 * 14px letter box, lighter than the size-26 version per user review; its
 * adv_w 13.2px is closest to the letters' 12px. Shift it down (14-9)/2 =
 * 2.5 -> 3px so its center matches the letters. */
#define RAIN_GRIPPER_V_OFFSET 3

/* Random-drop model: a drop is a brightness pulse that travels down one
 * column. Drops spawn at random times on random columns with random speeds
 * — no periodic schedule, no seamless-loop requirement. */
#define RAIN_MAX_DROPS 6
#define RAIN_SPAWN_CHANCE 20 /* percent per frame (sparser, slower drops) */
#define RAIN_TRAIL_S 3

/* Mono_20 adv_w = 192/16 = 12px; +3px gap = 15px column pitch. */
#define RAIN_CELL_W 15
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
/* Landscape showkey cell (60,112) 200x56 cannot fit 4 rows at the portrait
 * 23px pitch (4x23=92 > 56), so use 3 rows — same row/col spacing as
 * portrait (23px pitch, 3px gap), losing only the bottom "A S T R A" row.
 * Canvas bottom 181 overlaps the lower wpm/scanner cell (174) by 7px,
 * mirroring portrait's accepted 7px overlap. */
#define RAIN_ROWS 3
#define RAIN_CELL_H 23
#define RAIN_W (RAIN_COLS * RAIN_CELL_W) /* 75 */
#define RAIN_H (RAIN_ROWS * RAIN_CELL_H) /* 69 */
#define RAIN_X 122 /* 60 + (200 - 75) / 2 */
#define RAIN_Y 112
#else
/* 4 rows x 23px (20px row + 3px gap) = 92px. Y=141 (user: 5px up from 146);
 * bottom at 233 overlaps the scanner cell at 226 — accepted. */
#define RAIN_ROWS 4
#define RAIN_CELL_H 23
#define RAIN_W (RAIN_COLS * RAIN_CELL_W) /* 75 */
#define RAIN_H (RAIN_ROWS * RAIN_CELL_H) /* 92 */
#define RAIN_X 82 /* 44 + (152 - 75) / 2 */
#define RAIN_Y 141
#endif

/* Base color matches the screen root background so the block blends in. */
#define RAIN_COLOR_BASE ((lv_color_t)LV_COLOR_MAKE(0x0a, 0x0a, 0x0d))

/* Fixed gripper glyph string (U+EB04). Stored once; the matrix references it
 * by pointer identity so the draw loop can pick the icon font per cell. */
static const char rain_gripper[] = RAIN_GRIPPER;

/* The glyph matrix. Gripper slots are fixed icons; every other cell is a
 * single letter rendered with Mono_20. Landscape drops the bottom row to
 * keep the portrait 23px row pitch. */
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
static const char *const rain_matrix[RAIN_ROWS][RAIN_COLS] = {
    {rain_gripper, "E", "R", "G", "O"},
    {"a", "s", "t", "r", "a"},
    {"e", "r", "g", "o", rain_gripper},
};
#else
static const char *const rain_matrix[RAIN_ROWS][RAIN_COLS] = {
    {rain_gripper, "E", "R", "G", "O"},
    {"a", "s", "t", "r", "a"},
    {"e", "r", "g", "o", rain_gripper},
    {"A", "S", "T", "R", "A"},
};
#endif

/* Active drops. pos is the head position in rows (float, can exceed
 * RAIN_ROWS-1 as it leaves the bottom); speed is rows per frame. */
struct rain_drop
{
    bool active;
    uint8_t col;
    float pos;
    float speed;
};

static struct rain_drop rain_drops[RAIN_MAX_DROPS];

static lv_color_t rain_lut[10];
/* Per-cell static text buffers: lv_draw_label defers rendering until
 * lv_canvas_finish_layer(), so every glyph needs its own storage — a single
 * shared buffer would render the last cell's character in every cell. */
static char rain_chars[RAIN_ROWS][RAIN_COLS][2];

LV_DRAW_BUF_DEFINE_STATIC(rain_buf, RAIN_W, RAIN_H, LV_COLOR_FORMAT_RGB565);

static lv_color_t rain_lerp(lv_color_t a, lv_color_t b, uint8_t t)
{
    lv_color_t c;
    c.red = (uint8_t)(a.red + ((int)(b.red - a.red) * t) / 255);
    c.green = (uint8_t)(a.green + ((int)(b.green - a.green) * t) / 255);
    c.blue = (uint8_t)(a.blue + ((int)(b.blue - a.blue) * t) / 255);
    return c;
}

/* Rebuild the 10-level LUT from the current theme accent: index 0 = dimmed
 * accent (idle glyph), index 9 = brightened accent (drop head).
 * lv_color_darken(c, lvl) = mix(black, c, lvl) keeps lvl/255 of the source,
 * so darken(200) ≈ 22% brightness — clearly dimmer than the head. The head
 * is lightened toward white (mix with 16%) so the drops pop off the dark
 * background. */
static void rain_rebuild_lut(void)
{
    lv_color_t accent = theme_accent_color();
    lv_color_t dim = lv_color_darken(accent, 200);       /* ~22% brightness */
    lv_color_t bright = lv_color_lighten(accent, 40);    /* ~84% + 16% white */
    for (uint8_t i = 0; i < 10; i++)
    {
        rain_lut[i] = rain_lerp(dim, bright, (uint8_t)(i * 255 / 9));
    }
}

/* Spawn a new drop on a random column with a random speed, if a slot is free.
 * Avoids columns that already have an active drop so one column does not get
 * repeated drops back-to-back. */
static void rain_spawn_drop(void)
{
    bool col_busy[RAIN_COLS] = {false};
    uint8_t free = RAIN_COLS;

    for (uint8_t i = 0; i < RAIN_MAX_DROPS; i++)
    {
        if (rain_drops[i].active && !col_busy[rain_drops[i].col])
        {
            col_busy[rain_drops[i].col] = true;
            free--;
        }
    }

    if (free == 0)
    {
        return;
    }

    uint8_t target = sys_rand32_get() % free;
    for (uint8_t c = 0; c < RAIN_COLS; c++)
    {
        if (!col_busy[c] && target-- == 0)
        {
            target = c;
            break;
        }
    }

    for (uint8_t i = 0; i < RAIN_MAX_DROPS; i++)
    {
        if (!rain_drops[i].active)
        {
            rain_drops[i].active = true;
            rain_drops[i].col = target;
            rain_drops[i].pos = -1.0f;
            rain_drops[i].speed = 0.18f + (float)(sys_rand32_get() % 100) / 1000.0f;
            return;
        }
    }
}

/* Advance all drops by one frame; despawn those that left the bottom. */
static void rain_advance_drops(void)
{
    for (uint8_t i = 0; i < RAIN_MAX_DROPS; i++)
    {
        if (!rain_drops[i].active)
        {
            continue;
        }
        rain_drops[i].pos += rain_drops[i].speed;
        if (rain_drops[i].pos > RAIN_ROWS + RAIN_TRAIL_S)
        {
            rain_drops[i].active = false;
        }
    }
}

/* Brightness 0..255 for cell (row, col): max over drops in this column of
 * the head/trail profile. */
static uint8_t rain_brightness(uint8_t row, uint8_t col)
{
    uint8_t best = 0;

    for (uint8_t i = 0; i < RAIN_MAX_DROPS; i++)
    {
        if (!rain_drops[i].active || rain_drops[i].col != col)
        {
            continue;
        }

        float p = rain_drops[i].pos;
        float b;
        if (p < row)
        {
            continue; /* head not reached this row yet */
        }
        if (p <= row + 1)
        {
            b = p - row; /* arrival ramp 0 -> 1 */
        }
        else
        {
            float d = p - row - 1; /* trail decay */
            b = 1.0f - d / RAIN_TRAIL_S;
            if (b < 0.0f)
            {
                b = 0.0f;
            }
        }

        uint8_t v = (uint8_t)(b * 255.0f + 0.5f);
        if (v > best)
        {
            best = v;
        }
    }
    return best;
}

/* Redraw the whole canvas for the current frame. LVGL-only code, runs on the
 * display thread (timer callback). Draw tasks are dispatched synchronously by
 * lv_canvas_finish_layer, so the static text buffers are safe. */
static void rain_draw_frame(struct zmk_widget_rain_status *widget)
{
    lv_canvas_fill_bg(widget->obj, RAIN_COLOR_BASE, LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(widget->obj, &layer);

    for (uint8_t r = 0; r < RAIN_ROWS; r++)
    {
        for (uint8_t c = 0; c < RAIN_COLS; c++)
        {
            const char *text = rain_matrix[r][c];
            bool gripper = (text == rain_gripper);
            const lv_font_t *font = gripper ? &Gripper_20 : &Mono_20;

            if (!gripper)
            {
                /* Per-cell storage: lv_draw_label defers rendering until
                 * lv_canvas_finish_layer(), so a single shared buffer would
                 * render the last cell's character in every cell. The fixed
                 * gripper string is static and needs no copy. */
                rain_chars[r][c][0] = text[0];
                rain_chars[r][c][1] = '\0';
                text = rain_chars[r][c];
            }

            uint8_t v = rain_brightness(r, c);
            lv_color_t color = rain_lut[v * 9 / 255];

            lv_draw_label_dsc_t dsc;
            lv_draw_label_dsc_init(&dsc);
            dsc.font = font;
            dsc.color = color;
            dsc.text = text;
            dsc.align = LV_TEXT_ALIGN_CENTER;

            /* Label area = the cell; LVGL centers the glyph's line box inside
             * it, so a 21px Mono_20 line in a 20px portrait cell trims 1px
             * (landscape's 14px rows crop harder by design). */
            int32_t vy = r * RAIN_CELL_H;
            if (gripper)
            {
                /* The U+EB04 gripper glyph's bitmap occupies only the top 7
                 * of its 14px box (row 7..13 are empty), while Mono_20 letter
                 * bitmaps fill the whole box. Push the gripper down so its
                 * visual center aligns with the letters. */
                vy += RAIN_GRIPPER_V_OFFSET;
            }
            lv_area_t area = {
                .x1 = (lv_coord_t)(c * RAIN_CELL_W),
                .y1 = (lv_coord_t)vy,
                .x2 = (lv_coord_t)(c * RAIN_CELL_W + RAIN_CELL_W - 1),
                .y2 = (lv_coord_t)MIN(vy + RAIN_CELL_H - 1, RAIN_H - 1),
            };
            lv_draw_label(&layer, &dsc, &area);
        }
    }

    lv_canvas_finish_layer(widget->obj, &layer);
}

/* Opacity fade on the canvas object (mirrors showkey_status's anim pattern). */
static void rain_fade_exec_cb(void *var, int32_t v)
{
    struct zmk_widget_rain_status *w = var;
    lv_obj_set_style_opa(w->obj, (lv_opa_t)v, LV_PART_MAIN);
}

static void rain_fade_out_done_cb(lv_anim_t *a)
{
    struct zmk_widget_rain_status *widget = lv_anim_get_user_data(a);
    widget->visible = false;
    lv_timer_pause(widget->timer);
    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(widget->obj, LV_OPA_COVER, LV_PART_MAIN);
}

static void rain_start_fade_in(struct zmk_widget_rain_status *widget)
{
    widget->visible = true;
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_anim_delete(widget, NULL); /* no stacked fades */

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, widget);
    lv_anim_set_user_data(&a, widget);
    lv_anim_set_exec_cb(&a, rain_fade_exec_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, RAIN_FADE_IN_MS);
    lv_anim_start(&a);

    lv_timer_resume(widget->timer);
}

static void rain_start_fade_out(struct zmk_widget_rain_status *widget)
{
    lv_opa_t cur = lv_obj_get_style_opa(widget->obj, LV_PART_MAIN);
    lv_anim_delete(widget, NULL);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, widget);
    lv_anim_set_user_data(&a, widget);
    lv_anim_set_exec_cb(&a, rain_fade_exec_cb);
    lv_anim_set_values(&a, cur, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, RAIN_FADE_OUT_MS);
    lv_anim_set_completed_cb(&a, rain_fade_out_done_cb);
    lv_anim_start(&a);
}

static void rain_frame_cb(lv_timer_t *timer)
{
    struct zmk_widget_rain_status *widget = lv_timer_get_user_data(timer);
    if (!widget->visible)
    {
        return;
    }

    if ((sys_rand32_get() % 100) < RAIN_SPAWN_CHANCE)
    {
        rain_spawn_drop();
    }
    rain_advance_drops();
    rain_draw_frame(widget);
}

/* Gate: hidden rain fades in 10s after the last key press. Rain has its own
 * idle timer and does not consult the theme sleep state (30s), so the two
 * timers stay decoupled. */
static void rain_gate_cb(lv_timer_t *timer)
{
    struct zmk_widget_rain_status *widget = lv_timer_get_user_data(timer);
    if (widget->visible || widget->key_pressed)
    {
        return;
    }
    if (k_uptime_get() - widget->last_activity_ms > RAIN_IDLE_TIMEOUT_MS)
    {
        rain_start_fade_in(widget);
    }
}

struct rain_status_state
{
    bool pressed;
};

static struct rain_status_state get_state(const zmk_event_t *_eh)
{
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(_eh);

    return (struct rain_status_state){.pressed = ev != NULL && ev->state};
}

static void rain_status_update_cb(struct rain_status_state state)
{
    struct zmk_widget_rain_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        if (state.pressed)
        {
            widget->key_pressed = true;
            widget->last_activity_ms = k_uptime_get();
            if (widget->visible)
            {
                rain_start_fade_out(widget);
            }
        }
        else
        {
            widget->key_pressed = false;
        }
    }
}

/* Rebuild the LUT when the theme accent fades (red <-> cyan). */
static void rain_status_refresh(void)
{
    rain_rebuild_lut();
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_rain_status, struct rain_status_state,
                            rain_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_rain_status, zmk_keycode_state_changed);

int zmk_widget_rain_status_init(struct zmk_widget_rain_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_canvas_create(parent);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_pos(widget->obj, RAIN_X, RAIN_Y);
    lv_obj_set_size(widget->obj, RAIN_W, RAIN_H);

    rain_rebuild_lut();

    LV_DRAW_BUF_INIT_STATIC(rain_buf);
    lv_canvas_set_draw_buf(widget->obj, &rain_buf);
    lv_canvas_fill_bg(widget->obj, RAIN_COLOR_BASE, LV_OPA_COVER);

    widget->visible = false;
    widget->key_pressed = false;
    widget->last_activity_ms = k_uptime_get();

    widget->timer = lv_timer_create(rain_frame_cb, RAIN_FRAME_MS, widget);
    lv_timer_pause(widget->timer);
    widget->gate_timer = lv_timer_create(rain_gate_cb, RAIN_GATE_POLL_MS, widget);

    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);

    sys_slist_append(&widgets, &widget->node);

    theme_register_refresh(rain_status_refresh);

    widget_rain_status_init();
    return 0;
}

lv_obj_t *zmk_widget_rain_status_obj(struct zmk_widget_rain_status *widget)
{
    return widget->obj;
}
