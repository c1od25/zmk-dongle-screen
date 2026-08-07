/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "custom_status_screen.h"

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
#include "widgets/output_status.h"
static struct zmk_widget_output_status output_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
#include "widgets/layer_status.h"
static struct zmk_widget_layer_status layer_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
#include "widgets/battery_status.h"
static struct zmk_widget_dongle_battery_status dongle_battery_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
#include "widgets/wpm_status.h"
static struct zmk_widget_wpm_status wpm_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
#include "widgets/mod_status.h"
static struct zmk_widget_mod_status mod_widget;
#endif

#if CONFIG_DONGLE_SCREEN_SHOWKEY_ACTIVE
#include "widgets/showkey_status.h"
static struct zmk_widget_showkey_status showkey_status_widget;
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

lv_style_t global_style;

#if CONFIG_DONGLE_SCREEN_MEM_DEBUG
static void mem_debug_timer_cb(lv_timer_t *timer)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    LOG_INF("LVGL mem: total=%u max_used=%u used_pct=%u%% free=%u frag_pct=%u%%",
            mon.total_size, mon.max_used, mon.used_pct,
            mon.free_size, mon.frag_pct);
}
#endif

/*
 * Screen assembly (design doc §2 / §3).
 *
 * Each widget owns the layout of its own container, so the screen file creates
 * the root, applies the background, and calls the inits. Explicit positions
 * only — no flex/grid anywhere.
 *
 * Positioning contract per widget (verified against each src/widgets/*.c init):
 *   output    full-width top bar (240/320 x 22); screen aligns TOP_MID(0,10)
 *             — the usb(10,10) / bt(184,10) cells are placed inside the bar by
 *             the widget itself (lv_obj_align TOP_LEFT/TOP_RIGHT + dot).
 *   layer     self-positions its container (66,10) 108x22 / (60,10) 200x22.
 *   battery   full-panel (240x320 / 320x240); self-positions the two columns
 *             (lbat / rbat). Origin at panel (0,0) — no alignment needed.
 *   mod       sizes its container (108x82 / 200x56) but does NOT self-position;
 *             the screen places it at the mods cell (66,50) / (60,50). The four
 *             keys are positioned relative to the container origin by the widget.
 *   showkey   self-positions its container (66,138) 108x82 / (60,112) 200x56.
 *   wpm       self-positions its container (66,226) 108x82 / (60,174) 200x56.
 *
 * Z-order (later children render on top): top bar (output + layer) first, then
 * the top separator line, then the battery columns, then the center stack
 * (mods / showkey / wpm). The battery panel is transparent apart from its two
 * columns, so nothing below it is occluded.
 */
lv_obj_t *zmk_display_status_screen()
{
    lv_obj_t *screen;

    screen = lv_obj_create(NULL);
    /* Root background: bg0 #0a0a0d, fully opaque, no padding (design §4
     * `style_screen_bg`). LVGL 9 split-pad API: pad_all no longer exists. */
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0a0a0d), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(screen, 0, LV_PART_MAIN);

    lv_style_init(&global_style);
    // lv_style_set_text_font(&global_style, &lv_font_unscii_8); // ToDo: Font is not recognized
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);

    /* Top bar — output widget (full-width) first. */
#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
    zmk_widget_layer_status_init(&layer_status_widget, screen);
#endif

    /* Top separator line (design §2.3 / §3.3 `topsep`): a filled 1 px line in
     * the `border` color #2a2a34 at y=38. The design's 6 px row with a 1 px
     * bottom border (style_top_sep) is simplified to a filled 1 px line for a
     * crisper render with the same palette color. */
    lv_obj_t *topsep = lv_obj_create(screen);
    lv_obj_remove_style_all(topsep);
    lv_obj_remove_flag(topsep, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(topsep, lv_color_hex(0x2a2a34), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(topsep, LV_OPA_COVER, LV_PART_MAIN);
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
    lv_obj_set_pos(topsep, 10, 38);
    lv_obj_set_size(topsep, 300, 1);
#else
    lv_obj_set_pos(topsep, 10, 38);
    lv_obj_set_size(topsep, 220, 1);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen);
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen);
    /* The mod container is sized by the widget but not positioned — place it at
     * the design's mods cell. */
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
    lv_obj_set_pos(zmk_widget_mod_status_obj(&mod_widget), 60, 50);
#else
    lv_obj_set_pos(zmk_widget_mod_status_obj(&mod_widget), 66, 50);
#endif
#endif

#if CONFIG_DONGLE_SCREEN_SHOWKEY_ACTIVE
    zmk_widget_showkey_status_init(&showkey_status_widget, screen);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
#endif

#if CONFIG_DONGLE_SCREEN_MEM_DEBUG
    lv_timer_create(mem_debug_timer_cb, 10000, NULL);
#endif

    return screen;
}
