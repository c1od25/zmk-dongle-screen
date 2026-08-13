/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>
#include <zmk/display.h>
#include <zmk/usb.h>

#include "battery_status.h"
#include "../brightness.h"
#include <fonts.h>
#include <theme.h>

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
#define SOURCE_OFFSET 1
#else
#define SOURCE_OFFSET 0
#endif

#define BATTERY_SLOT_COUNT (ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT + SOURCE_OFFSET)

extern const struct zmk_split_transport_central *active_transport;

/*
 * Battery slots are connection-order based, not physical-side based:
 * slot 0 is the first-paired peripheral and slot 1 the second. The dongle has
 * no way to know which half is physically left or right, so the tags render the
 * slot ordinal ("L" for slot 0, "R" for slot 1) to match the design mockup.
 */

/*
 * Layout geometry (design doc §2.4 portrait / §3.4 landscape).
 * The widget self-positions its two battery columns (lbat / rbat); it is sized
 * to the full panel so the screen file's BOTTOM_MID alignment is a no-op and
 * the widget origin lands at panel (0,0).
 * BATTERY_BAR_Y follows the top bar shift (portrait +5 / landscape +3) so the
 * bar top keeps its 40 px gap to the moved topsep in both orientations; the
 * bar bottom stays glued to the tag top via the runtime bar_h formula.
 */
#define BATTERY_BAR_W         26
#define BATTERY_TAG_GAP       4 /* tag-top ↔ bar-bottom gap (both orientations) */
#define BATTERY_ICON_Y        54

#if CONFIG_DONGLE_SCREEN_HORIZONTAL
#define BATTERY_SCREEN_W      320
#define BATTERY_SCREEN_H      240
#define BATTERY_TAG_BOTTOM    227 /* WPM value bottom (landscape) */
#define BATTERY_TAG_BOTTOM_OFF -13 /* TAG_BOTTOM - SCREEN_H */
#define BATTERY_BAR_Y         81 /* topsep at 41 → bar top 40 px below */
#define BATTERY_SLOT0_BAR_X   17
#define BATTERY_SLOT1_BAR_X   277
#define BATTERY_SLOT0_CENTER_X 30
#define BATTERY_SLOT1_CENTER_X 290
#else
#define BATTERY_SCREEN_W      240
#define BATTERY_SCREEN_H      320
#define BATTERY_TAG_BOTTOM    305 /* WPM value bottom (portrait) */
#define BATTERY_TAG_BOTTOM_OFF -15 /* TAG_BOTTOM - SCREEN_H */
#define BATTERY_BAR_Y         83 /* topsep at 43 → bar top 40 px below */
#define BATTERY_SLOT0_BAR_X   20
#define BATTERY_SLOT1_BAR_X   194
#define BATTERY_SLOT0_CENTER_X 33
#define BATTERY_SLOT1_CENTER_X 207
#endif

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/*
 * Per-slot widget objects: "NN%" percent text (battery glyphs U+F240-F244 are
 * NOT in the committed NerdFonts — font audit), a vertical lv_bar, and the
 * slot designator tag. One entry per peripheral slot.
 */
struct battery_object
{
    lv_obj_t *icon;
    lv_obj_t *bar;
    lv_obj_t *tag;
    char text[8]; /* per-slot stable storage; lv_label_set_text_static() does NOT copy */
} battery_objects[BATTERY_SLOT_COUNT];

/* Bar styles (design doc §4): track on LV_PART_MAIN, tier on LV_PART_INDICATOR. */
static lv_style_t style_bar_track;
static lv_style_t style_bar_hi;
static lv_style_t style_bar_lo;

enum battery_bar_tier
{
    BATTERY_BAR_HI,
    BATTERY_BAR_LO,
};

static void init_bar_styles(void)
{
    lv_style_init(&style_bar_track);
    lv_style_set_bg_color(&style_bar_track, lv_color_hex(0x1a1a20));
    lv_style_set_bg_opa(&style_bar_track, LV_OPA_COVER);
    lv_style_set_radius(&style_bar_track, 3);

    lv_style_init(&style_bar_hi);
    lv_style_set_bg_color(&style_bar_hi, lv_color_hex(0x3a3a44));
    lv_style_set_bg_grad_color(&style_bar_hi, lv_color_hex(0x9a9aa5));
    lv_style_set_bg_grad_dir(&style_bar_hi, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&style_bar_hi, LV_OPA_COVER);
    lv_style_set_radius(&style_bar_hi, 3);

    lv_style_init(&style_bar_lo);
    lv_style_set_bg_color(&style_bar_lo, theme_accent_color());
    lv_style_set_bg_opa(&style_bar_lo, LV_OPA_COVER);
    lv_style_set_radius(&style_bar_lo, 3);
}

static void set_bar_tier(lv_obj_t *bar, enum battery_bar_tier tier)
{
    lv_obj_remove_style(bar, &style_bar_hi, LV_PART_INDICATOR);
    lv_obj_remove_style(bar, &style_bar_lo, LV_PART_INDICATOR);
    switch (tier)
    {
    case BATTERY_BAR_LO:
        lv_obj_add_style(bar, &style_bar_lo, LV_PART_INDICATOR);
        break;
    case BATTERY_BAR_HI:
    default:
        lv_obj_add_style(bar, &style_bar_hi, LV_PART_INDICATOR);
        break;
    }
}

/* True when the transport reports the slot's peripheral as connected. The
 * transport connection state is authoritative and independent of the battery
 * event pipeline, so the L/R/X tags never get stuck on a stale battery level. */
static bool is_slot_connected(uint8_t source)
{
    if (source < SOURCE_OFFSET || source >= BATTERY_SLOT_COUNT)
    {
        return false;
    }

    if (!active_transport || !active_transport->api || !active_transport->api->get_available_source_ids)
    {
        return false;
    }

    uint8_t sources[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];
    int count = active_transport->api->get_available_source_ids(sources);
    if (count < 0)
    {
        return false;
    }

    for (int i = 0; i < count; i++)
    {
        if (sources[i] == source - SOURCE_OFFSET)
        {
            return true;
        }
    }

    return false;
}

/* Directly draw the given level for a slot to the LVGL widgets. No filtering,
 * buffering, or animation: this widget is poll-driven and renders the current
 * state snapshot each tick. A connected slot always shows its L/R tag even if
 * the battery level is still unknown (cache 0) — only a transport disconnect
 * renders the red X. */
static void battery_display_render(uint8_t source, uint8_t level)
{
    struct battery_object *slot = &battery_objects[source];
    if (slot->bar == NULL)
    {
        return;
    }

    bool connected = is_slot_connected(source);

    if (!connected)
    {
        /* Disconnected: empty bar, red "X" tag (design §1). */
        set_bar_tier(slot->bar, BATTERY_BAR_LO);
        lv_obj_set_style_text_color(slot->icon, theme_accent_color(), 0);
        lv_obj_set_style_text_color(slot->tag, theme_accent_color(), 0);
        lv_label_set_text_static(slot->tag, "X");
        lv_bar_set_value(slot->bar, 0, LV_ANIM_OFF);
        lv_label_set_text_static(slot->icon, "--");
        return;
    }

    /* Connected. Tag shows the slot designator; bar/percent show the level.
     * A zero cache (battery not yet reported) renders "--" instead of a fake
     * 0% so the user can tell "connected, level unknown" from "disconnected". */
    if (level >= 30)
    {
        set_bar_tier(slot->bar, BATTERY_BAR_HI);
        lv_obj_set_style_text_color(slot->icon, lv_color_hex(0x9a9aa5), 0);
    }
    else
    {
        set_bar_tier(slot->bar, BATTERY_BAR_LO);
        lv_obj_set_style_text_color(slot->icon, theme_accent_color(), 0);
    }

    lv_label_set_text_static(slot->tag, source == 0 ? "L" : "R");
    lv_obj_set_style_text_color(slot->tag, lv_color_hex(0x9a9aa5), 0);

    if (level >= 1)
    {
        lv_bar_set_value(slot->bar, level, LV_ANIM_OFF);
        snprintf(slot->text, sizeof(slot->text), "%d%%", level);
        lv_label_set_text_static(slot->icon, slot->text);
    }
    else
    {
        lv_bar_set_value(slot->bar, 0, LV_ANIM_OFF);
        lv_label_set_text_static(slot->icon, "--");
    }
}

static void battery_status_refresh(void)
{
    lv_style_set_bg_color(&style_bar_lo, theme_accent_color());
    lv_obj_report_style_change(&style_bar_lo);
}

/* 1s poll: the single driver of this widget. Each tick re-reads the transport
 * connection state and the central's cached battery levels and re-renders the
 * affected slots, so a connect/disconnect shows within one second regardless of
 * whether any battery event was delivered (boot reads may fail or be dropped). */
static void battery_status_poll_cb(lv_timer_t *timer)
{
    static bool prev_connected[BATTERY_SLOT_COUNT];

    for (uint8_t i = 0; i < BATTERY_SLOT_COUNT; i++)
    {
        uint8_t level = 0;
        bool has_level = zmk_split_central_get_peripheral_battery_level(i - SOURCE_OFFSET, &level) == 0;

        bool connected = is_slot_connected(i);
        if (connected && !prev_connected[i])
        {
            LOG_INF("Peripheral slot %d connected, waking screen", i);
            brightness_wake_screen_on_reconnect();
        }
        prev_connected[i] = connected;

        if (!has_level)
        {
            level = 0;
        }

        battery_display_render(i, level);
    }
}

int zmk_widget_dongle_battery_status_init(struct zmk_widget_dongle_battery_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    /*
     * The widget owns the two battery columns and self-positions them.
     * Sized to the full panel so the screen file's BOTTOM_MID alignment is a
     * no-op and the widget origin stays at panel (0,0).
     */
    lv_obj_set_size(widget->obj, BATTERY_SCREEN_W, BATTERY_SCREEN_H);

    init_bar_styles();

    const int32_t slot_bar_x[2] = {BATTERY_SLOT0_BAR_X, BATTERY_SLOT1_BAR_X};
    const int32_t slot_center_x[2] = {BATTERY_SLOT0_CENTER_X, BATTERY_SLOT1_CENTER_X};

    for (int i = 0; i < BATTERY_SLOT_COUNT; i++)
    {
        battery_objects[i] = (struct battery_object){0};
        if (i >= 2)
        {
            /* The design defines exactly two battery columns (lbat/rbat). */
            continue;
        }

        /* Vertical battery bar (design §2.4 / §3.4). Height computed at runtime
         * so the bar bottom lands BATTERY_TAG_GAP above the tag top in BOTH
         * orientations — Mono_20->line_height is the actual rendered height,
         * not an assumed constant. */
        lv_obj_t *bar = lv_bar_create(widget->obj);
        int32_t tag_h = Mono_20.line_height;
        int32_t bar_h = BATTERY_TAG_BOTTOM - tag_h - BATTERY_TAG_GAP - BATTERY_BAR_Y;
        lv_obj_set_size(bar, BATTERY_BAR_W, bar_h);
        lv_obj_set_pos(bar, slot_bar_x[i], BATTERY_BAR_Y);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_bar_set_orientation(bar, LV_BAR_ORIENTATION_VERTICAL);
        lv_obj_add_style(bar, &style_bar_track, LV_PART_MAIN);
        lv_obj_add_style(bar, &style_bar_lo, LV_PART_INDICATOR);

        /* Percent text icon — battery glyphs U+F240..F244 are NOT in the
         * committed NerdFonts (font audit), so render the level as text.
         * Uses the default Mono_20 (digits/percent are not in NerdFonts). */
        lv_obj_t *icon = lv_label_create(widget->obj);
        lv_obj_set_style_text_font(icon, &Mono_20, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(0x9a9aa5), 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, slot_center_x[i] - BATTERY_SCREEN_W / 2,
                     BATTERY_ICON_Y);
        lv_label_set_text_static(icon, "--");

        /* Slot designator tag. Starts as "X" (not-connected) — battery_display
         * render flips it to "L"/"R" once the slot is connected. Bottom-relative
         * alignment puts the tag bottom exactly on the WPM value bottom
         * (portrait 305, landscape 227) regardless of font line-height. */
        lv_obj_t *tag = lv_label_create(widget->obj);
        lv_obj_set_style_text_font(tag, &Mono_20, 0);
        lv_obj_set_style_text_color(tag, lv_color_hex(0x9a9aa5), 0);
        lv_obj_align(tag, LV_ALIGN_BOTTOM_MID, slot_center_x[i] - BATTERY_SCREEN_W / 2,
                     BATTERY_TAG_BOTTOM_OFF);
        lv_label_set_text_static(tag, "X");

        battery_objects[i] = (struct battery_object){
            .icon = icon,
            .bar = bar,
            .tag = tag,
        };
    }

    sys_slist_append(&widgets, &widget->node);

    theme_register_refresh(battery_status_refresh);

    /* 1s poll: the sole driver of connection + battery display. */
    widget->poll_timer = lv_timer_create(battery_status_poll_cb, 1000, widget);

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget)
{
    return widget->obj;
}
