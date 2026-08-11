/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

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
 * Geometry
 *
 * The design's 3x11 "ERGOASTRA" diagonal grid (112x313) cannot fit the
 * showkey cell, so we render a dense window of it: 3 columns x RAIN_ROWS
 * rows, each column still one row lower than the previous (diagonal), with
 * a Mono_20 letter in every cell. Portrait and landscape use different
 * windows because the cells have very different aspect ratios. Every letter
 * is Mono_20 (~12x15px glyph, 21px line height) — clearly legible, and the
 * canvas sits fully inside the showkey cell, so nothing overlaps.
 * ---------------------------------------------------------------------- */
#define RAIN_WORD "ERGOASTRA"
#define RAIN_WORD_LEN 9

#define RAIN_COLS 3
#define RAIN_FRAMES 60
#define RAIN_FRAME_MS 50
#define RAIN_LUT_N 10 /* brightness levels, lerp(DARK, RED, i/9) */

#define RAIN_FADE_IN_MS 1500
#define RAIN_FADE_OUT_MS 250
#define RAIN_GATE_POLL_MS 500

/* Rain-drop model (design doc §2/§3.2): head advances 1 row per 3 frames,
 * 48 frames per drop, 3-row trail, fixed 60-frame schedule. */
#define RAIN_ROW_STEPS 3
#define RAIN_FALL 48
#define RAIN_TRAIL_S 3

#if CONFIG_DONGLE_SCREEN_HORIZONTAL
/* showkey cell (60,112) 200x56. 3 cols x 2 rows of the diagonal ERGOASTRA
 * grid; 56px column pitch spreads across the wide cell with even gaps. */
#define RAIN_CELL_W 56
#define RAIN_CELL_H 24
#define RAIN_ROWS 2
#define RAIN_ROW_OFFSET 2
#define RAIN_W (RAIN_COLS * RAIN_CELL_W) /* 168 */
#define RAIN_H (RAIN_ROWS * RAIN_CELL_H) /* 48 */
#define RAIN_X 76
#define RAIN_Y 116
#else
/* showkey cell (44,138) 152x82. 3 cols x 4 rows of the diagonal ERGOASTRA
 * grid; 40px column pitch = 3 columns spanning 120px, centered with even
 * 16px margins; 20px row pitch keeps Mono_20 glyphs legible. */
#define RAIN_CELL_W 40
#define RAIN_CELL_H 20
#define RAIN_ROWS 4
#define RAIN_ROW_OFFSET 3
#define RAIN_W (RAIN_COLS * RAIN_CELL_W) /* 120 */
#define RAIN_H (RAIN_ROWS * RAIN_CELL_H) /* 80 */
#define RAIN_X 60
#define RAIN_Y 139
#endif

/* Strict palette — the only colors the animation may use. BASE matches the
 * screen root background (custom_status_screen.c 0x0a0a0d) so the rain block
 * blends with the rest of the UI instead of showing a gray panel. */
#define RAIN_COLOR_BASE ((lv_color_t)LV_COLOR_MAKE(0x0a, 0x0a, 0x0d)) /* 界面背景 */
#define RAIN_COLOR_DARK ((lv_color_t)LV_COLOR_MAKE(0x5b, 0x1d, 0x1a)) /* 静止暗红 */
#define RAIN_COLOR_RED ((lv_color_t)LV_COLOR_MAKE(0xef, 0x4d, 0x43))  /* 峰值红 */

/* Fixed per-column drop schedule (design §3.2): 0xFF = no second drop.
 * Gaps: col1 34, col2 23 (both >= 20); no two drops start on the same frame. */
static const uint8_t rain_drops[RAIN_COLS][2] = {
    {42, 0xFF},
    {5, 39},
    {22, 45},
};

static lv_color_t rain_lut[RAIN_LUT_N];
/* Per-cell static text buffers: lv_draw_label defers rendering until
 * lv_canvas_finish_layer(), so every glyph needs its own storage — a single
 * shared buffer would render the last cell's character in every cell. */
static char rain_chars[RAIN_COLS][RAIN_ROWS][2];

LV_DRAW_BUF_DEFINE_STATIC(rain_buf, RAIN_W, RAIN_H, LV_COLOR_FORMAT_RGB565);

static lv_color_t rain_lerp(lv_color_t a, lv_color_t b, uint8_t t)
{
    lv_color_t c;
    c.red = (uint8_t)(a.red + ((int)(b.red - a.red) * t) / 255);
    c.green = (uint8_t)(a.green + ((int)(b.green - a.green) * t) / 255);
    c.blue = (uint8_t)(a.blue + ((int)(b.blue - a.blue) * t) / 255);
    return c;
}

/* Column c, design row r, frame f -> brightness 0..255 (design §2.3). */
static uint8_t rain_brightness(uint8_t c, uint8_t r, uint8_t f)
{
    uint8_t best = 0;

    for (uint8_t k = 0; k < 2; k++)
    {
        uint8_t t0 = rain_drops[c][k];
        if (t0 == 0xFF)
        {
            break;
        }

        int16_t age = (f - t0 + RAIN_FRAMES) % RAIN_FRAMES;
        if (age >= RAIN_FALL)
        {
            continue;
        }

        float pos = age / (float)RAIN_ROW_STEPS - 1.0f;
        float b;
        if (pos < r)
        {
            continue; /* head has not reached this row yet */
        }
        if (pos <= r + 1)
        {
            b = pos - r; /* arrival ramp 0 -> 1 */
        }
        else
        {
            float d = pos - r - 1; /* trail decay */
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
 * lv_canvas_finish_layer, so the static text buffer is safe. */
static void rain_draw_frame(struct zmk_widget_rain_status *widget)
{
    lv_canvas_fill_bg(widget->obj, RAIN_COLOR_BASE, LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(widget->obj, &layer);

    const int32_t line_h = lv_font_get_line_height(&Mono_20);
    const int32_t v_ofs = (RAIN_CELL_H - line_h) / 2;

    for (uint8_t c = 0; c < RAIN_COLS; c++)
    {
        for (uint8_t r = 0; r < RAIN_ROWS; r++)
        {
            uint8_t dr = r + RAIN_ROW_OFFSET; /* design-grid row */
            int16_t gi = (int16_t)dr - (int16_t)c;
            if (gi < 0 || gi >= RAIN_WORD_LEN)
            {
                continue; /* empty corner -> stays base color (no gripper) */
            }

            uint8_t v = rain_brightness(c, dr, widget->frame);
            lv_color_t color = rain_lut[((int)v * (RAIN_LUT_N - 1) + 127) / 255];

            rain_chars[c][r][0] = RAIN_WORD[gi];
            rain_chars[c][r][1] = '\0';

            lv_draw_label_dsc_t dsc;
            lv_draw_label_dsc_init(&dsc);
            dsc.font = &Mono_20;
            dsc.color = color;
            dsc.text = rain_chars[c][r];
            dsc.align = LV_TEXT_ALIGN_CENTER;

            lv_area_t area = {
                .x1 = (lv_coord_t)(c * RAIN_CELL_W),
                .y1 = (lv_coord_t)(r * RAIN_CELL_H + v_ofs),
                .x2 = (lv_coord_t)(c * RAIN_CELL_W + RAIN_CELL_W - 1),
                .y2 = (lv_coord_t)MIN(r * RAIN_CELL_H + v_ofs + line_h - 1, RAIN_H - 1),
            };
            lv_draw_label(&layer, &dsc, &area);
        }
    }

    lv_canvas_finish_layer(widget->obj, &layer);
    widget->frame = (widget->frame + 1) % RAIN_FRAMES;
}

/* Opacity fade on the canvas object (mirrors showkey_status's anim pattern:
 * anim var == user data == widget, so lv_anim_delete(widget, NULL) works). */
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
    rain_draw_frame(widget);
}

/* Gate: hidden rain waits until the keyboard is asleep (30s no key press —
 * theme_is_asleep(), by which time the showkey text has faded out long ago).
 * The extra key_pressed guard covers a key held across the 30s boundary. */
static void rain_gate_cb(lv_timer_t *timer)
{
    struct zmk_widget_rain_status *widget = lv_timer_get_user_data(timer);
    if (widget->visible)
    {
        return;
    }
    if (theme_is_asleep() && !widget->key_pressed)
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
        widget->key_pressed = state.pressed;
        if (state.pressed && widget->visible)
        {
            rain_start_fade_out(widget);
        }
    }
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

    /* Precompute the 10-level brightness LUT: lerp(0x5B1D1A, 0xEF4D43, i/9). */
    for (uint8_t i = 0; i < RAIN_LUT_N; i++)
    {
        rain_lut[i] = rain_lerp(RAIN_COLOR_DARK, RAIN_COLOR_RED,
                                (uint8_t)(i * 255 / (RAIN_LUT_N - 1)));
    }

    LV_DRAW_BUF_INIT_STATIC(rain_buf);
    lv_canvas_set_draw_buf(widget->obj, &rain_buf);
    lv_canvas_fill_bg(widget->obj, RAIN_COLOR_BASE, LV_OPA_COVER);

    widget->frame = 0;
    widget->visible = false;
    widget->key_pressed = false;

    widget->timer = lv_timer_create(rain_frame_cb, RAIN_FRAME_MS, widget);
    lv_timer_pause(widget->timer);
    widget->gate_timer = lv_timer_create(rain_gate_cb, RAIN_GATE_POLL_MS, widget);

    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);

    sys_slist_append(&widgets, &widget->node);

    widget_rain_status_init();
    return 0;
}

lv_obj_t *zmk_widget_rain_status_obj(struct zmk_widget_rain_status *widget)
{
    return widget->obj;
}
