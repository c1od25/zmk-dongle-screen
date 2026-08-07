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
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/usb.h>

#include "battery_status.h"
#include "../brightness.h"

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
#define SOURCE_OFFSET 1
#else
#define SOURCE_OFFSET 0
#endif

#define BATTERY_SLOT_COUNT (ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT + SOURCE_OFFSET)

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
 */
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
#define BATTERY_SCREEN_W      320
#define BATTERY_SCREEN_H      240
#define BATTERY_BAR_H         128
#define BATTERY_TAG_Y         214
#define BATTERY_SLOT0_BAR_X   21
#define BATTERY_SLOT1_BAR_X   281
#define BATTERY_SLOT0_CENTER_X 30
#define BATTERY_SLOT1_CENTER_X 290
#else
#define BATTERY_SCREEN_W      240
#define BATTERY_SCREEN_H      320
#define BATTERY_BAR_H         188
#define BATTERY_TAG_Y         292
#define BATTERY_SLOT0_BAR_X   24
#define BATTERY_SLOT1_BAR_X   198
#define BATTERY_SLOT0_CENTER_X 33
#define BATTERY_SLOT1_CENTER_X 207
#endif

#define BATTERY_BAR_W         18
#define BATTERY_BAR_Y         78
#define BATTERY_ICON_Y        54

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct battery_state
{
    uint8_t source;
    uint8_t level;
    bool usb_present;
};

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
} battery_objects[BATTERY_SLOT_COUNT];

/* Peripheral reconnection tracking
 * ZMK sends battery events with level < 1 when peripherals disconnect
 */
static int8_t last_battery_levels[BATTERY_SLOT_COUNT];

/* Bar styles (design doc §4): track on LV_PART_MAIN, tier on LV_PART_INDICATOR. */
static lv_style_t style_bar_track;
static lv_style_t style_bar_hi;
static lv_style_t style_bar_mid;
static lv_style_t style_bar_lo;

enum battery_bar_tier
{
    BATTERY_BAR_HI,
    BATTERY_BAR_MID,
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

    lv_style_init(&style_bar_mid);
    lv_style_set_bg_color(&style_bar_mid, lv_color_hex(0x4a3a3a));
    lv_style_set_bg_grad_color(&style_bar_mid, lv_color_hex(0x7a2a26));
    lv_style_set_bg_grad_dir(&style_bar_mid, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&style_bar_mid, LV_OPA_COVER);
    lv_style_set_radius(&style_bar_mid, 3);

    lv_style_init(&style_bar_lo);
    lv_style_set_bg_color(&style_bar_lo, lv_color_hex(0xe8453c));
    lv_style_set_bg_opa(&style_bar_lo, LV_OPA_COVER);
    lv_style_set_radius(&style_bar_lo, 3);
}

static void set_bar_tier(lv_obj_t *bar, enum battery_bar_tier tier)
{
    lv_obj_remove_style(bar, &style_bar_hi, LV_PART_INDICATOR);
    lv_obj_remove_style(bar, &style_bar_mid, LV_PART_INDICATOR);
    lv_obj_remove_style(bar, &style_bar_lo, LV_PART_INDICATOR);
    switch (tier)
    {
    case BATTERY_BAR_MID:
        lv_obj_add_style(bar, &style_bar_mid, LV_PART_INDICATOR);
        break;
    case BATTERY_BAR_LO:
        lv_obj_add_style(bar, &style_bar_lo, LV_PART_INDICATOR);
        break;
    case BATTERY_BAR_HI:
    default:
        lv_obj_add_style(bar, &style_bar_hi, LV_PART_INDICATOR);
        break;
    }
}

static void init_peripheral_tracking(void)
{
    for (int i = 0; i < BATTERY_SLOT_COUNT; i++)
    {
        last_battery_levels[i] = -1; // -1 indicates never seen before
    }
}

static bool is_peripheral_reconnecting(uint8_t source, uint8_t new_level)
{
    if (source >= BATTERY_SLOT_COUNT)
    {
        return false;
    }

    int8_t previous_level = last_battery_levels[source];

    // Reconnection detected if:
    // 1. Previous level was < 1 (disconnected/unknown) AND
    // 2. New level is >= 1 (valid battery level)
    bool reconnecting = (previous_level < 1) && (new_level >= 1);

    if (reconnecting)
    {
        LOG_INF("Peripheral %d reconnection: %d%% -> %d%% (was %s)",
                source, previous_level, new_level,
                previous_level == -1 ? "never seen" : "disconnected");
    }

    return reconnecting;
}

static void set_battery_symbol(lv_obj_t *widget, struct battery_state state)
{
    if (state.source >= BATTERY_SLOT_COUNT)
    {
        return;
    }

    struct battery_object *slot = &battery_objects[state.source];
    if (slot->bar == NULL)
    {
        return;
    }

    // Check for reconnection using the existing battery level mechanism
    bool reconnecting = is_peripheral_reconnecting(state.source, state.level);

    // Update our tracking
    last_battery_levels[state.source] = state.level;

    // Wake screen on reconnection
    if (reconnecting)
    {
#if CONFIG_DONGLE_SCREEN_IDLE_TIMEOUT_S > 0
        LOG_INF("Peripheral %d reconnected (battery: %d%%), requesting screen wake",
                state.source, state.level);
        brightness_wake_screen_on_reconnect();
#else
        LOG_INF("Peripheral %d reconnected (battery: %d%%)",
                state.source, state.level);
#endif
    }

    if (state.level < 1)
    {
        /* Disconnected: empty bar, red "X" tag (design §1). */
        lv_bar_set_value(slot->bar, 0, LV_ANIM_OFF);
        set_bar_tier(slot->bar, BATTERY_BAR_LO);
        lv_obj_set_style_text_color(slot->icon, lv_color_hex(0xe8453c), 0);
        lv_label_set_text_static(slot->icon, "0%");
        lv_obj_set_style_text_color(slot->tag, lv_color_hex(0xe8453c), 0);
        lv_label_set_text_static(slot->tag, "X");
        return;
    }

    LOG_DBG("source: %d, level: %d, usb: %d", state.source, state.level, state.usb_present);

    /* Bar fill (no animation on updates) + tier: >50 hi, >20 mid, <=20 lo. */
    lv_bar_set_value(slot->bar, state.level, LV_ANIM_OFF);
    if (state.level > 50)
    {
        set_bar_tier(slot->bar, BATTERY_BAR_HI);
    }
    else if (state.level > 20)
    {
        set_bar_tier(slot->bar, BATTERY_BAR_MID);
    }
    else
    {
        set_bar_tier(slot->bar, BATTERY_BAR_LO);
    }

    /* Icon: "NN%" percent text, color per level (design §1 table). */
    static char batt_text[8];
    snprintf(batt_text, sizeof(batt_text), "%u%%", state.level);
    lv_label_set_text_static(slot->icon, batt_text);
    if (state.level > 45)
    {
        lv_obj_set_style_text_color(slot->icon, lv_color_hex(0x9a9aa5), 0);
    }
    else if (state.level > 20)
    {
        lv_obj_set_style_text_color(slot->icon, lv_color_hex(0x7a2a26), 0);
    }
    else
    {
        lv_obj_set_style_text_color(slot->icon, lv_color_hex(0xe8453c), 0);
    }

    /* Tag: restore the slot designator after a disconnect "X". */
    lv_label_set_text_static(slot->tag, state.source == 0 ? "L" : "R");
    lv_obj_set_style_text_color(slot->tag, lv_color_hex(0x9a9aa5), 0);
}

void battery_status_update_cb(struct battery_state state)
{
    struct zmk_widget_dongle_battery_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_symbol(widget->obj, state); }
}

static struct battery_state peripheral_battery_status_get_state(const zmk_event_t *eh)
{
    const struct zmk_peripheral_battery_state_changed *ev = as_zmk_peripheral_battery_state_changed(eh);
    return (struct battery_state){
        .source = ev->source + SOURCE_OFFSET,
        .level = ev->state_of_charge,
    };
}

static struct battery_state central_battery_status_get_state(const zmk_event_t *eh)
{
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_state){
        .source = 0,
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

static struct battery_state battery_status_get_state(const zmk_event_t *eh)
{
    if (as_zmk_peripheral_battery_state_changed(eh) != NULL)
    {
        return peripheral_battery_status_get_state(eh);
    }
    else
    {
        return central_battery_status_get_state(eh);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_dongle_battery_status, struct battery_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_peripheral_battery_state_changed);

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
#endif /* !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
#endif /* IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY) */

int zmk_widget_dongle_battery_status_init(struct zmk_widget_dongle_battery_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    /*
     * The widget owns the two battery columns and self-positions them.
     * Sized to the full panel so the screen file's BOTTOM_MID alignment is a
     * no-op and the widget origin stays at panel (0,0).
     */
    lv_obj_set_size(widget->obj, BATTERY_SCREEN_W, BATTERY_SCREEN_H);

    init_bar_styles();

    const lv_coord_t slot_bar_x[2] = {BATTERY_SLOT0_BAR_X, BATTERY_SLOT1_BAR_X};
    const lv_coord_t slot_center_x[2] = {BATTERY_SLOT0_CENTER_X, BATTERY_SLOT1_CENTER_X};

    for (int i = 0; i < BATTERY_SLOT_COUNT; i++)
    {
        battery_objects[i] = (struct battery_object){0};
        if (i >= 2)
        {
            /* The design defines exactly two battery columns (lbat/rbat). */
            continue;
        }

        /* Vertical battery bar (design §2.4 / §3.4). */
        lv_obj_t *bar = lv_bar_create(widget->obj);
        lv_obj_set_size(bar, BATTERY_BAR_W, BATTERY_BAR_H);
        lv_obj_set_pos(bar, slot_bar_x[i], BATTERY_BAR_Y);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_bar_set_orientation(bar, LV_BAR_ORIENTATION_VERTICAL);
        lv_obj_add_style(bar, &style_bar_track, LV_PART_MAIN);
        lv_obj_add_style(bar, &style_bar_lo, LV_PART_INDICATOR);

        /* Percent text icon — battery glyphs U+F240..F244 are NOT in the
         * committed NerdFonts (font audit), so render the level as text.
         * Uses the default montserrat_20 (digits/percent are not in NerdFonts). */
        lv_obj_t *icon = lv_label_create(widget->obj);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(0x9a9aa5), 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, slot_center_x[i] - BATTERY_SCREEN_W / 2,
                     BATTERY_ICON_Y);
        lv_label_set_text_static(icon, "--");

        /* Slot designator tag ("L" / "R"). */
        lv_obj_t *tag = lv_label_create(widget->obj);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(tag, lv_color_hex(0x9a9aa5), 0);
        lv_obj_align(tag, LV_ALIGN_TOP_MID, slot_center_x[i] - BATTERY_SCREEN_W / 2,
                     BATTERY_TAG_Y);
        lv_label_set_text_static(tag, i == 0 ? "L" : "R");

        battery_objects[i] = (struct battery_object){
            .icon = icon,
            .bar = bar,
            .tag = tag,
        };
    }

    sys_slist_append(&widgets, &widget->node);

    // Initialize peripheral tracking
    init_peripheral_tracking();

    widget_dongle_battery_status_init();

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget)
{
    return widget->obj;
}
