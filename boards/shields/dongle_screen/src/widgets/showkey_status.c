/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/hid_indicators.h>
#include <dt-bindings/zmk/hid_indicators.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/modifiers.h>

#include "showkey_status.h"
#include <fonts.h>
#include <theme.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct showkey_status_state
{
    bool pressed;
    uint16_t usage_page;
    uint32_t keycode;
};

enum showkey_kind
{
    SHOWKEY_TEXT,      /* Mono_48 name only */
    SHOWKEY_ICON,      /* Nerd Font icon only */
    SHOWKEY_SIDE_ICON, /* L/R prefix (Mono_48) + Nerd Font icon */
};

struct showkey_lookup
{
    enum showkey_kind kind;
    const char *side;       /* "L"/"R" for SHOWKEY_SIDE_ICON */
    const char *icon;       /* Nerd Font PUA glyph */
    const char *text;       /* Mono_48 name */
    const lv_font_t *font;  /* icon font; NULL = NerdFonts_Regular_48 */
};

/* Nerd Font PUA glyphs — codepoints verified against nerd-fonts-generated.css
 * and present in NerdFonts_Regular_48 (see src/fonts/NerdFonts_Regular_48.c). */
#define ICON_CTRL  "\U000F0634" /* nf-md-apple_keyboard_control */
#define ICON_SHIFT "\U000F0636" /* nf-md-apple_keyboard_shift */
#define ICON_ALT   "\U000F0635" /* nf-md-apple_keyboard_option */
#define ICON_GUI   "\U000F0633" /* nf-md-apple_keyboard_command */
#define ICON_UP    "\U000F0737" /* nf-md-arrow_up_bold */
#define ICON_DOWN  "\U000F072E" /* nf-md-arrow_down_bold */
#define ICON_LEFT  "\U000F0731" /* nf-md-arrow_left_bold */
#define ICON_RIGHT "\U000F0734" /* nf-md-arrow_right_bold */
#define ICON_SPACE "\U000F1050" /* nf-md-keyboard_space */
#define ICON_ENTER "\U000F0311" /* nf-md-keyboard_return */
#define ICON_BSPC  "\U000F030D" /* nf-md-keyboard_backspace */
#define ICON_TAB   "\U000F0312" /* nf-md-keyboard_tab */
#define ICON_ESC   "\U000F12B7" /* nf-md-keyboard_esc */
#define ICON_CAPS  "\U000F030E" /* nf-md-keyboard_caps */
#define ICON_VOL_UP "\U000F075D" /* nf-md-volume_plus — encoder wheel up */
#define ICON_VOL_DN "\U000F075E" /* nf-md-volume_minus — encoder wheel down */

/* HID usage (keyboard page 0x07) → icon (with optional L/R side prefix). */
static const struct key_icon
{
    uint8_t usage;
    enum showkey_kind kind;
    const char *side;
    const char *icon;
} key_icons[] = {
    {0xE0, SHOWKEY_SIDE_ICON, "L", ICON_CTRL},  {0xE1, SHOWKEY_SIDE_ICON, "L", ICON_SHIFT},
    {0xE2, SHOWKEY_SIDE_ICON, "L", ICON_ALT},   {0xE3, SHOWKEY_SIDE_ICON, "L", ICON_GUI},
    {0xE4, SHOWKEY_SIDE_ICON, "R", ICON_CTRL},  {0xE5, SHOWKEY_SIDE_ICON, "R", ICON_SHIFT},
    {0xE6, SHOWKEY_SIDE_ICON, "R", ICON_ALT},   {0xE7, SHOWKEY_SIDE_ICON, "R", ICON_GUI},
    {0x4F, SHOWKEY_ICON, NULL, ICON_RIGHT},     {0x50, SHOWKEY_ICON, NULL, ICON_LEFT},
    {0x51, SHOWKEY_ICON, NULL, ICON_DOWN},      {0x52, SHOWKEY_ICON, NULL, ICON_UP},
    {0x2C, SHOWKEY_ICON, NULL, ICON_SPACE},     {0x28, SHOWKEY_ICON, NULL, ICON_ENTER},
    {0x2A, SHOWKEY_ICON, NULL, ICON_BSPC},      {0x2B, SHOWKEY_ICON, NULL, ICON_TAB},
    {0x29, SHOWKEY_ICON, NULL, ICON_ESC},       {0x39, SHOWKEY_ICON, NULL, ICON_CAPS},
};

/* Remaining non-icon keys keep their concise Mono_48 names. */
static const struct key_name
{
    uint8_t usage;
    const char *name;
} key_names[] = {
    {0x2D, "MINUS"}, {0x2E, "="},    {0x2F, "["},    {0x30, "]"},
    {0x31, "\\"},    {0x33, ";"},    {0x34, "'"},    {0x35, "`"},
    {0x36, ","},     {0x37, "."},    {0x38, "/"},    {0x46, "PRTSC"},
    {0x47, "SCRLK"}, {0x48, "PAUSE"},{0x49, "INS"},  {0x4A, "HOME"},
    {0x4B, "PGUP"},  {0x4C, "DEL"},  {0x4D, "END"},  {0x4E, "PGDN"},
    {0x53, "NUMLK"}, {0x54, "KP/"},  {0x55, "KP*"},  {0x56, "KP-"},
    {0x57, "KP+"},   {0x58, "KPENT"},{0x59, "KP1"},  {0x5A, "KP2"},
    {0x5B, "KP3"},   {0x5C, "KP4"},  {0x5D, "KP5"},  {0x5E, "KP6"},
    {0x5F, "KP7"},   {0x60, "KP8"},  {0x61, "KP9"},  {0x62, "KP0"},
    {0x63, "KP."},
};

/* Shift-aware plain/shifted char pairs for letters, digits and punctuation.
 * Shift state comes from the central's live HID keyboard report
 * (zmk_hid_get_keyboard_report()->body.modifiers), which reflects the
 * modifiers held on ANY half of the split — the same state the host
 * receives. The keycode event's own implicit/explicit_modifiers fields only
 * describe mods baked into that key's binding (e.g. LS(SEMI)) and can never
 * tell us about Shift held on the other half, so they are not used here. */
struct shift_pair
{
    uint8_t usage;
    char plain;
    char shifted;
};

static const struct shift_pair shift_pairs[] = {
    /* 0x04-0x1D: letters a-z / A-Z */
    {0x04, 'a', 'A'}, {0x05, 'b', 'B'}, {0x06, 'c', 'C'}, {0x07, 'd', 'D'},
    {0x08, 'e', 'E'}, {0x09, 'f', 'F'}, {0x0A, 'g', 'G'}, {0x0B, 'h', 'H'},
    {0x0C, 'i', 'I'}, {0x0D, 'j', 'J'}, {0x0E, 'k', 'K'}, {0x0F, 'l', 'L'},
    {0x10, 'm', 'M'}, {0x11, 'n', 'N'}, {0x12, 'o', 'O'}, {0x13, 'p', 'P'},
    {0x14, 'q', 'Q'}, {0x15, 'r', 'R'}, {0x16, 's', 'S'}, {0x17, 't', 'T'},
    {0x18, 'u', 'U'}, {0x19, 'v', 'V'}, {0x1A, 'w', 'W'}, {0x1B, 'x', 'X'},
    {0x1C, 'y', 'Y'}, {0x1D, 'z', 'Z'},
    /* 0x1E-0x27: digits 1-0 */
    {0x1E, '1', '!'}, {0x1F, '2', '@'}, {0x20, '3', '#'}, {0x21, '4', '$'},
    {0x22, '5', '%'}, {0x23, '6', '^'}, {0x24, '7', '&'}, {0x25, '8', '*'},
    {0x26, '9', '('}, {0x27, '0', ')'},
    /* 0x2D-0x38: punctuation (0x32 non-US #/~ intentionally omitted) */
    {0x2D, '-', '_'}, {0x2E, '=', '+'}, {0x2F, '[', '{'}, {0x30, ']', '}'},
    {0x31, '\\', '|'}, {0x33, ';', ':'}, {0x34, '\'', '"'}, {0x35, '`', '~'},
    {0x36, ',', '<'}, {0x37, '.', '>'}, {0x38, '/', '?'},
};

/* Static text buffer — lv_label_set_text_static() does NOT copy. */
static char showkey_buf[16];

static struct showkey_lookup lookup_showkey(uint16_t usage_page, uint32_t keycode, bool shift_held,
                                            bool caps_lock)
{
    struct showkey_lookup r = {.kind = SHOWKEY_TEXT, .text = NULL};
    uint32_t u = keycode;

    if (usage_page == HID_USAGE_CONSUMER)
    {
        /* Consumer-page keys (encoder volume wheel): nf-md-volume_plus/minus.
         * These live in the dedicated Volume_48 font (they aren't among the
         * 14 glyphs embedded in NerdFonts_Regular_48). */
        switch (u)
        {
        case HID_USAGE_CONSUMER_VOLUME_INCREMENT:
            r.kind = SHOWKEY_ICON;
            r.icon = ICON_VOL_UP;
            r.font = &Volume_48;
            return r;
        case HID_USAGE_CONSUMER_VOLUME_DECREMENT:
            r.kind = SHOWKEY_ICON;
            r.icon = ICON_VOL_DN;
            r.font = &Volume_48;
            return r;
        default:
            return r;
        }
    }

    if (usage_page != HID_USAGE_KEY)
    {
        return r;
    }

    for (size_t i = 0; i < ARRAY_SIZE(shift_pairs); i++)
    {
        if (shift_pairs[i].usage == u)
        {
            /* HID semantics: letters a-z (0x04-0x1D) render uppercase when
             * Shift XOR Caps Lock is active; digits/punctuation only follow
             * Shift (Caps Lock has no effect on them). */
            bool letter = u >= 0x04 && u <= 0x1D;
            bool shifted = shift_held ^ (letter && caps_lock);
            showkey_buf[0] = shifted ? shift_pairs[i].shifted : shift_pairs[i].plain;
            showkey_buf[1] = '\0';
            r.text = showkey_buf;
            return r;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(key_icons); i++)
    {
        if (key_icons[i].usage == u)
        {
            r.kind = key_icons[i].kind;
            r.side = key_icons[i].side;
            r.icon = key_icons[i].icon;
            return r;
        }
    }

    if (u >= 0x3A && u <= 0x45) /* F1-F12 */
    {
        snprintf(showkey_buf, sizeof(showkey_buf), "F%u", (unsigned)(u - 0x3A + 1));
        r.text = showkey_buf;
    }
    else
    {
        for (size_t i = 0; i < ARRAY_SIZE(key_names); i++)
        {
            if (key_names[i].usage == u)
            {
                r.text = key_names[i].name;
                break;
            }
        }
    }
    return r;
}

static struct showkey_status_state get_state(const zmk_event_t *_eh)
{
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(_eh);

    return (struct showkey_status_state){
        .pressed = ev && ev->state,
        .usage_page = ev ? ev->usage_page : 0,
        .keycode = ev ? ev->keycode : 0,
    };
}

static void showkey_fade_cb(void *widget, int32_t v)
{
    struct zmk_widget_showkey_status *w = widget;
    lv_obj_set_style_opa(w->label, (lv_opa_t)v, LV_PART_MAIN);
    lv_obj_set_style_opa(w->icon_label, (lv_opa_t)v, LV_PART_MAIN);
}

static void showkey_fade_done_cb(lv_anim_t *a)
{
    struct zmk_widget_showkey_status *widget = lv_anim_get_user_data(a);
    if (widget != NULL)
    {
        widget->hold_timer = NULL;
    }
    lv_label_set_text_static(widget->label, "");
    lv_label_set_text_static(widget->icon_label, "");
    lv_obj_set_style_opa(widget->label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(widget->icon_label, LV_OPA_COVER, LV_PART_MAIN);
}

/* Released key stays visible for the hold delay, then fades out (255→0, 400ms). */
static void showkey_hold_timeout(lv_timer_t *t)
{
    struct zmk_widget_showkey_status *widget = lv_timer_get_user_data(t);
    widget->hold_timer = NULL;

    lv_anim_delete(widget, NULL); /* no stacked fades */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, widget);
    lv_anim_set_user_data(&a, widget);
    lv_anim_set_exec_cb(&a, showkey_fade_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_completed_cb(&a, showkey_fade_done_cb);
    lv_anim_start(&a);
}

/* SHOWKEY_SIDE_ICON: center the L/R prefix and icon as one group in the cell. */
static void showkey_align(struct zmk_widget_showkey_status *widget, struct showkey_lookup *r)
{
    if (r->kind == SHOWKEY_SIDE_ICON)
    {
        const char *side = lv_label_get_text(widget->label);
        const char *icon = lv_label_get_text(widget->icon_label);
        lv_coord_t side_w =
            lv_text_get_width(side, (uint32_t)strlen(side),
                              lv_obj_get_style_text_font(widget->label, LV_PART_MAIN),
                              lv_obj_get_style_text_letter_space(widget->label, LV_PART_MAIN));
        lv_coord_t icon_w =
            lv_text_get_width(icon, (uint32_t)strlen(icon),
                              lv_obj_get_style_text_font(widget->icon_label, LV_PART_MAIN),
                              lv_obj_get_style_text_letter_space(widget->icon_label, LV_PART_MAIN));
        lv_coord_t gap = 4;
        lv_coord_t half = (side_w + gap + icon_w) / 2;
        lv_obj_align(widget->label, LV_ALIGN_CENTER, -(half - side_w / 2), 0);
        lv_obj_align(widget->icon_label, LV_ALIGN_CENTER, (half - icon_w / 2), 0);
    }
    else if (r->kind == SHOWKEY_ICON)
    {
        lv_obj_align(widget->icon_label, LV_ALIGN_CENTER, 0, 0);
    }
    else
    {
        lv_obj_align(widget->label, LV_ALIGN_CENTER, 0, 0);
    }
}

static void showkey_apply(struct zmk_widget_showkey_status *widget, struct showkey_lookup *r)
{
    if (r->kind == SHOWKEY_TEXT)
    {
        lv_label_set_text_static(widget->label, r->text != NULL ? r->text : "KEY");
        lv_label_set_text_static(widget->icon_label, "");
    }
    else
    {
        lv_label_set_text_static(widget->label, r->side != NULL ? r->side : "");
        lv_label_set_text_static(widget->icon_label, r->icon != NULL ? r->icon : "");
        /* Volume icons use their own font; everything else uses the default. */
        lv_obj_set_style_text_font(widget->icon_label,
                                   r->font != NULL ? r->font : &NerdFonts_Regular_48,
                                   LV_PART_MAIN);
    }
    showkey_align(widget, r);
}

static void showkey_status_update_cb(struct showkey_status_state state)
{
    /* Read the central's live HID report — the single cross-half source of
     * truth (hid_listener builds it from every half's keycode events, same
     * bytes the host receives). Shift held on the OTHER half is visible here,
     * which the keycode event's own modifier fields can never tell us. */
    bool shift_held =
        (zmk_hid_get_keyboard_report()->body.modifiers & (MOD_LSFT | MOD_RSFT)) != 0;
    /* Caps Lock is reported by the host via the USB HID LED report
     * (zmk_hid_indicators_changed); it applies to letters only and is
     * combined with Shift inside lookup_showkey. */
    bool caps_lock =
        (zmk_hid_indicators_get_current_profile() & HID_INDICATOR_CAPS_LOCK) != 0;

    struct zmk_widget_showkey_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        if (state.pressed)
        {
            if (widget->hold_timer != NULL)
            {
                lv_timer_delete(widget->hold_timer);
                widget->hold_timer = NULL;
            }
            lv_anim_delete(widget, NULL);
            lv_obj_set_style_opa(widget->label, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_opa(widget->icon_label, LV_OPA_COVER, LV_PART_MAIN);

            struct showkey_lookup r =
                lookup_showkey(state.usage_page, state.keycode, shift_held, caps_lock);
            showkey_apply(widget, &r);
            lv_obj_set_style_text_color(widget->label, theme_accent_color(), LV_PART_MAIN);
            lv_obj_set_style_text_color(widget->icon_label, theme_accent_color(), LV_PART_MAIN);
        }
        else
        {
            if (lv_label_get_text(widget->label)[0] == '\0' &&
                lv_label_get_text(widget->icon_label)[0] == '\0')
            {
                continue;
            }
            if (widget->hold_timer != NULL)
            {
                lv_timer_delete(widget->hold_timer);
                widget->hold_timer = NULL;
            }
            lv_anim_delete(widget, NULL);
            widget->hold_timer = lv_timer_create(showkey_hold_timeout, 800, widget);
            lv_timer_set_repeat_count(widget->hold_timer, 1);
        }
    }
}

static void showkey_status_refresh(void)
{
    struct zmk_widget_showkey_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        if (lv_label_get_text(widget->label)[0] != '\0' ||
            lv_label_get_text(widget->icon_label)[0] != '\0')
        {
            lv_obj_set_style_text_color(widget->label, theme_accent_color(), LV_PART_MAIN);
            lv_obj_set_style_text_color(widget->icon_label, theme_accent_color(), LV_PART_MAIN);
        }
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_showkey_status, struct showkey_status_state,
                            showkey_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_showkey_status, zmk_keycode_state_changed);

int zmk_widget_showkey_status_init(struct zmk_widget_showkey_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(widget->obj);

#if CONFIG_DONGLE_SCREEN_HORIZONTAL
    lv_obj_set_size(widget->obj, 200, 56);
    lv_obj_set_pos(widget->obj, 60, 112);
#else
    /* Portrait: widened to fit a 5-char name at Mono_48 (5*28.8 + 4 letter
     * spaces = 148px) while keeping the stack's center axis (screen 240/2).
     * Center at x=120 → 120 - 152/2 = 44. */
    lv_obj_set_size(widget->obj, 152, 82);
    lv_obj_set_pos(widget->obj, 44, 138);
#endif

    /* Mono_Italic_48 (full ASCII) — text keys plus the L/R prefix for mods. */
    widget->label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label, &Mono_Italic_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->label, lv_color_hex(0xececef), LV_PART_MAIN);
    lv_label_set_text_static(widget->label, "");
    lv_obj_align(widget->label, LV_ALIGN_CENTER, 0, 0);

    /* NerdFonts_Regular_48 — icon glyphs (mods/arrows/special keys). */
    widget->icon_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->icon_label, &NerdFonts_Regular_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->icon_label, lv_color_hex(0xececef), LV_PART_MAIN);
    lv_label_set_text_static(widget->icon_label, "");
    lv_obj_align(widget->icon_label, LV_ALIGN_CENTER, 0, 0);

    sys_slist_append(&widgets, &widget->node);

    theme_register_refresh(showkey_status_refresh);

    widget_showkey_status_init();
    return 0;
}

lv_obj_t *zmk_widget_showkey_status_obj(struct zmk_widget_showkey_status *widget)
{
    return widget->obj;
}
