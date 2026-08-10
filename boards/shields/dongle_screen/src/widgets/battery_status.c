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
#include <fonts.h>
#include <theme.h>

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
    char text[8]; /* per-slot stable storage; lv_label_set_text_static() does NOT copy */
} battery_objects[BATTERY_SLOT_COUNT];

/* Peripheral reconnection tracking
 * ZMK sends battery events with level < 1 when peripherals disconnect
 */
static int8_t last_battery_levels[BATTERY_SLOT_COUNT];

/*
 * Battery SOC filtering: EMA (exponential moving average) + step limiter
 * + spike rejection. Fixed-point EMA with scale=256 avoids floating-point.
 * EMA_ALPHA_FP = 0.35 × 256 ≈ 90  →  ~3-sample equivalent smoothing.
 * STEP_LIMIT = 10  →  max change per update |10%%| to prevent wake jumps.
 * SPIKE_THRESHOLD = 25  →  single-sample jumps >25%% are rejected (a real
 *   battery cannot physically change that much in 120 s).
 * On reconnect (sleep→wake), the raw level is used directly to avoid a
 * multi-update ramp that would take minutes at the 120 s report interval.
 */
#define EMA_ALPHA_FP     90
#define EMA_SCALE        256
#define STEP_LIMIT       10
#define SPIKE_THRESHOLD  45

static int32_t filtered_level[BATTERY_SLOT_COUNT];
static bool     just_reconnected[BATTERY_SLOT_COUNT]; /* wake: skip spike check for first post-wake value */

/*
 * Display buffer: processed (filtered) values are held for DISPLAY_HOLD_MS
 * before rendering, so rapid successive updates coalesce into one refresh
 * instead of flickering on screen. A per-slot lv_timer restarts on every
 * update, committing only the final value when it fires.
 */
#define DISPLAY_HOLD_MS 1000
#define PENDING_NONE    (-1)

static int8_t  pending_level[BATTERY_SLOT_COUNT];
static lv_timer_t *pending_timer[BATTERY_SLOT_COUNT];

/*
 * Bar value animation: the processed level transitions from the current bar
 * value to the target over BAR_ANIM_MS. Wake (rising) uses an overshoot
 * path for a lively elastic feel; drain (falling) uses ease-out so the bar
 * settles smoothly. A per-slot anim_level[] var is driven by lv_anim and the
 * exec callback pushes both the bar and the percent label in sync.
 */
#define BAR_ANIM_MS 800

static int32_t anim_level[BATTERY_SLOT_COUNT];

static void battery_anim_exec_cb(void *var, int32_t v)
{
    uint8_t source = (var == &anim_level[0]) ? 0 : 1;
    struct battery_object *slot = &battery_objects[source];
    if (slot->bar == NULL) return;

    int32_t clamped = v < 0 ? 0 : (v > 100 ? 100 : v);
    int32_t tens = ((clamped + 5) / 10) * 10; /* quantize to 10% steps */
    lv_bar_set_value(slot->bar, tens, LV_ANIM_OFF);
    snprintf(slot->text, sizeof(slot->text), "%d%%", (int)tens);
    lv_label_set_text_static(slot->icon, slot->text);
}

static void battery_anim_start(uint8_t source, int32_t target)
{
    struct battery_object *slot = &battery_objects[source];
    if (slot->bar == NULL) return;

    int32_t start = lv_bar_get_value(slot->bar);

    if (start == target) {
        return;
    }

    lv_anim_delete(&anim_level[source], battery_anim_exec_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &anim_level[source]);
    lv_anim_set_exec_cb(&a, battery_anim_exec_cb);
    lv_anim_set_values(&a, start, target);
    lv_anim_set_duration(&a, BAR_ANIM_MS);
    lv_anim_set_path_cb(&a, target > start ? lv_anim_path_overshoot : lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void battery_display_render(uint8_t source);

static uint8_t apply_filter(uint8_t source, uint8_t raw, bool reconnecting)
{
    int32_t f = filtered_level[source];

    if (raw < 1) {
        filtered_level[source] = 0;
        just_reconnected[source] = false;
        return 0;
    }

    if (raw > 100) raw = 100;

    if (reconnecting) {
        /* Wake: the peripheral has already settled + preheated its reading,
         * so accept the fresh value directly (no stale last-known-good). */
        just_reconnected[source] = true;
        filtered_level[source] = (int32_t)raw * EMA_SCALE;
        return raw;
    }

    if (just_reconnected[source]) {
        just_reconnected[source] = false;
        filtered_level[source] = (int32_t)raw * EMA_SCALE;
        return raw;
    }

    int32_t prev_display = f / EMA_SCALE;
    int32_t delta = (int32_t)raw - prev_display;

    if (delta > SPIKE_THRESHOLD || delta < -SPIKE_THRESHOLD) {
        return (uint8_t)prev_display;
    }

    int32_t raw_fp = (int32_t)raw * EMA_SCALE;

    int32_t ema = (EMA_ALPHA_FP * raw_fp + (EMA_SCALE - EMA_ALPHA_FP) * f) / EMA_SCALE;

    int32_t limit = STEP_LIMIT * EMA_SCALE;
    if (ema > f + limit) {
        ema = f + limit;
    } else if (ema < f - limit) {
        ema = f - limit;
    }

    filtered_level[source] = ema;
    return (uint8_t)(ema / EMA_SCALE);
}

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

/* Render the processed (filtered) level for a slot to the LVGL widgets. */
static void battery_display_render(uint8_t source)
{
    struct battery_object *slot = &battery_objects[source];
    if (slot->bar == NULL)
    {
        return;
    }

    int8_t level = pending_level[source];
    if (level < 1)
    {
        /* Disconnected: empty bar, red "X" tag (design §1). Bar still animates
         * down to 0 for a smooth drain; the X tag appears immediately. */
        set_bar_tier(slot->bar, BATTERY_BAR_LO);
        lv_obj_set_style_text_color(slot->icon, theme_accent_color(), 0);
        lv_obj_set_style_text_color(slot->tag, theme_accent_color(), 0);
        lv_label_set_text_static(slot->tag, "X");
        battery_anim_start(source, 0);
        return;
    }

    /* Tier + colors switch instantly; value animates to the target. */
    if (level < 30)
    {
        set_bar_tier(slot->bar, BATTERY_BAR_LO);
        lv_obj_set_style_text_color(slot->icon, theme_accent_color(), 0);
    }
    else
    {
        set_bar_tier(slot->bar, BATTERY_BAR_HI);
        lv_obj_set_style_text_color(slot->icon, lv_color_hex(0x9a9aa5), 0);
    }

    /* Tag: restore the slot designator after a disconnect "X". */
    lv_label_set_text_static(slot->tag, source == 0 ? "L" : "R");
    lv_obj_set_style_text_color(slot->tag, lv_color_hex(0x9a9aa5), 0);

    battery_anim_start(source, level);
}

static void battery_pending_cb(lv_timer_t *timer)
{
    /* Identify the slot from the user_data passed at creation; the pointer
     * comparison is fragile because the timer may fire after pending_timer[]
     * was cleared, and a stale looping timer would be misidentified. */
    uint8_t source = (uint8_t)(uintptr_t)lv_timer_get_user_data(timer);
    pending_timer[source] = NULL;
    lv_timer_delete(timer);

    if (pending_level[source] == PENDING_NONE)
    {
        return;
    }

    battery_display_render(source);
    pending_level[source] = PENDING_NONE;
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

    state.level = apply_filter(state.source, state.level, reconnecting);

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

    LOG_DBG("source: %d, level: %d, usb: %d", state.source, state.level, state.usb_present);

    /* Disconnect must show immediately; buffered values render after the hold. */
    if (state.level < 1)
    {
        if (pending_timer[state.source] != NULL)
        {
            lv_timer_delete(pending_timer[state.source]);
            pending_timer[state.source] = NULL;
        }
        pending_level[state.source] = 0;
        battery_display_render(state.source);
        pending_level[state.source] = PENDING_NONE;
        return;
    }

    /* Stage the processed value and (re)start the display hold timer. */
    pending_level[state.source] = (int8_t)state.level;

    if (pending_timer[state.source] != NULL)
    {
        lv_timer_reset(pending_timer[state.source]);
    }
    else
    {
        pending_timer[state.source] = lv_timer_create(battery_pending_cb, DISPLAY_HOLD_MS,
                                                      (void *)(uintptr_t)state.source);
        /* One-shot: auto-deletes after firing, so a stale looping timer can
         * never linger and be misidentified as the other slot's callback. */
        lv_timer_set_repeat_count(pending_timer[state.source], 1);
    }
}

void battery_status_update_cb(struct battery_state state)
{
    struct zmk_widget_dongle_battery_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_symbol(widget->obj, state); }
}

static void battery_status_refresh(void)
{
    lv_style_set_bg_color(&style_bar_lo, theme_accent_color());
    lv_obj_report_style_change(&style_bar_lo);

    for (int i = 0; i < BATTERY_SLOT_COUNT; i++)
    {
        struct battery_object *slot = &battery_objects[i];
        if (slot->bar == NULL)
        {
            continue;
        }

        int8_t lvl = last_battery_levels[i];
        if (lvl == 0)
        {
            lv_obj_set_style_text_color(slot->icon, theme_accent_color(), 0);
            lv_obj_set_style_text_color(slot->tag, theme_accent_color(), 0);
        }
        else if (lvl > 0 && lvl < 30)
        {
            lv_obj_set_style_text_color(slot->icon, theme_accent_color(), 0);
        }
    }
}

static void battery_status_poll_cb(lv_timer_t *timer)
{
    uint8_t level = 0;
    bool changed = false;

    for (uint8_t i = 0; i < BATTERY_SLOT_COUNT; i++)
    {
        if (zmk_split_central_get_peripheral_battery_level(i, &level) != 0)
        {
            continue;
        }

        if (last_battery_levels[i] != (int8_t)level)
        {
            last_battery_levels[i] = level;
            set_battery_symbol(NULL, (struct battery_state){
                .source = i,
                .level = level,
            });
        }
    }
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

        /* Slot designator tag ("L" / "R"). Bottom-relative alignment puts the
         * tag bottom exactly on the WPM value bottom (portrait 305, landscape
         * 227) regardless of font line-height — TOP_MID absolute Y drifted
         * because Mono_20 line-height != assumed 30px. Mono_20 both orients. */
        lv_obj_t *tag = lv_label_create(widget->obj);
        lv_obj_set_style_text_font(tag, &Mono_20, 0);
        lv_obj_set_style_text_color(tag, lv_color_hex(0x9a9aa5), 0);
        lv_obj_align(tag, LV_ALIGN_BOTTOM_MID, slot_center_x[i] - BATTERY_SCREEN_W / 2,
                     BATTERY_TAG_BOTTOM_OFF);
        lv_label_set_text_static(tag, i == 0 ? "L" : "R");

        battery_objects[i] = (struct battery_object){
            .icon = icon,
            .bar = bar,
            .tag = tag,
        };
    }

    sys_slist_append(&widgets, &widget->node);

    theme_register_refresh(battery_status_refresh);

    // Initialize peripheral tracking
    init_peripheral_tracking();

    widget->poll_timer = lv_timer_create(battery_status_poll_cb, 1000, widget);

    widget_dongle_battery_status_init();

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget)
{
    return widget->obj;
}
