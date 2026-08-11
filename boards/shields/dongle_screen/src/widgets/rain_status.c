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
 * Geometry — fixed 4x5 glyph matrix, column pitch 12px = Mono_20 adv_w, so
 * glyphs abut with zero visual gap.
 *
 *   row0: E R G O .      row1: a s t r a
 *   row2: e r g o .      row3: A S T R A
 *
 * A '.' cell is an empty slot (kept at base color). The canvas sits fully
 * inside the showkey cell in both orientations.
 * ---------------------------------------------------------------------- */
#define RAIN_ROWS 4
#define RAIN_COLS 5
#define RAIN_FRAME_MS 50

#define RAIN_FADE_IN_MS 1500
#define RAIN_FADE_OUT_MS 250
#define RAIN_GATE_POLL_MS 500

/* Random-drop model: a drop is a brightness pulse that travels down one
 * column. Drops spawn at random times on random columns with random speeds
 * — no periodic schedule, no seamless-loop requirement. */
#define RAIN_MAX_DROPS 6
#define RAIN_SPAWN_CHANCE 40 /* percent per frame */
#define RAIN_TRAIL_S 3

#if CONFIG_DONGLE_SCREEN_HORIZONTAL
/* showkey cell (60,112) 200x56. 5 cols x 12px = 60, 4 rows x 14px = 56.
 * 14px row pitch trims ~1px off the Mono_20 glyph tops/bottoms so all 4
 * matrix rows fit the 56px-tall cell. */
#define RAIN_CELL_W 12
#define RAIN_CELL_H 14
#define RAIN_W (RAIN_COLS * RAIN_CELL_W) /* 60 */
#define RAIN_H (RAIN_ROWS * RAIN_CELL_H) /* 56 */
#define RAIN_X 130
#define RAIN_Y 112
#else
/* showkey cell (44,138) 152x82. 5 cols x 12px = 60, 4 rows x 20px = 80. */
#define RAIN_CELL_W 12
#define RAIN_CELL_H 20
#define RAIN_W (RAIN_COLS * RAIN_CELL_W) /* 60 */
#define RAIN_H (RAIN_ROWS * RAIN_CELL_H) /* 80 */
#define RAIN_X 90
#define RAIN_Y 139
#endif

/* Base color matches the screen root background so the block blends in. */
#define RAIN_COLOR_BASE ((lv_color_t)LV_COLOR_MAKE(0x0a, 0x0a, 0x0d))

/* The 4x5 glyph matrix. A '\0' entry is an empty slot (base color). */
static const char rain_matrix[RAIN_ROWS][RAIN_COLS] = {
    {'E', 'R', 'G', 'O', '\0'},
    {'a', 's', 't', 'r', 'a'},
    {'e', 'r', 'g', 'o', '\0'},
    {'A', 'S', 'T', 'R', 'A'},
};

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
 * accent (idle glyph), index 9 = full accent (drop head). Called on init and
 * whenever the theme accent changes. */
static void rain_rebuild_lut(void)
{
    lv_color_t accent = theme_accent_color();
    lv_color_t dim = lv_color_darken(accent, 180); /* ~30% brightness */
    for (uint8_t i = 0; i < 10; i++)
    {
        rain_lut[i] = rain_lerp(dim, accent, (uint8_t)(i * 255 / 9));
    }
}

/* Spawn a new drop on a random column with a random speed, if a slot is free. */
static void rain_spawn_drop(void)
{
    for (uint8_t i = 0; i < RAIN_MAX_DROPS; i++)
    {
        if (!rain_drops[i].active)
        {
            rain_drops[i].active = true;
            rain_drops[i].col = sys_rand32_get() % RAIN_COLS;
            rain_drops[i].pos = -1.0f;
            rain_drops[i].speed = 0.04f + (float)(sys_rand32_get() % 30) / 1000.0f;
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

    const int32_t line_h = lv_font_get_line_height(&Mono_20);
    const int32_t v_ofs = (RAIN_CELL_H - line_h) / 2;

    for (uint8_t r = 0; r < RAIN_ROWS; r++)
    {
        for (uint8_t c = 0; c < RAIN_COLS; c++)
        {
            char ch = rain_matrix[r][c];
            if (ch == '\0')
            {
                continue; /* empty slot stays base color */
            }

            uint8_t v = rain_brightness(r, c);
            lv_color_t color = rain_lut[v * 9 / 255];

            rain_chars[r][c][0] = ch;
            rain_chars[r][c][1] = '\0';

            lv_draw_label_dsc_t dsc;
            lv_draw_label_dsc_init(&dsc);
            dsc.font = &Mono_20;
            dsc.color = color;
            dsc.text = rain_chars[r][c];
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

/* Gate: hidden rain waits until the keyboard is asleep (theme_is_asleep(),
 * by which time the showkey text has faded out long ago). */
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
