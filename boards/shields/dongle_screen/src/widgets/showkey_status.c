/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

#include "showkey_status.h"
#include <fonts.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct showkey_status_state
{
    bool pressed;
    uint16_t usage_page;
    uint32_t keycode;
};

/* HID usage (keyboard page 0x07) → concise display name. Names are ≤6 chars so
 * the longest fit the show cell at Mono_20 (~9 chars @ 12px in 108px). */
static const struct key_name
{
    uint8_t usage;
    const char *name;
} key_names[] = {
    {0x28, "ENT"},    {0x29, "ESC"},   {0x2A, "BSPC"},  {0x2B, "TAB"},
    {0x2C, "SPC"},    {0x2D, "-"},     {0x2E, "="},     {0x2F, "["},
    {0x30, "]"},      {0x31, "\\"},    {0x33, ";"},     {0x34, "'"},
    {0x35, "`"},      {0x36, ","},     {0x37, "."},     {0x38, "/"},
    {0x39, "CAPS"},   {0x46, "PRTSC"}, {0x47, "SCRLK"}, {0x48, "PAUSE"},
    {0x49, "INS"},    {0x4A, "HOME"},  {0x4B, "PGUP"},  {0x4C, "DEL"},
    {0x4D, "END"},    {0x4E, "PGDN"},  {0x4F, "RIGHT"}, {0x50, "LEFT"},
    {0x51, "DOWN"},   {0x52, "UP"},    {0x53, "NUMLK"},
    {0x54, "KP/"},    {0x55, "KP*"},   {0x56, "KP-"},   {0x57, "KP+"},
    {0x58, "KPENT"},  {0x59, "KP1"},   {0x5A, "KP2"},   {0x5B, "KP3"},
    {0x5C, "KP4"},    {0x5D, "KP5"},   {0x5E, "KP6"},   {0x5F, "KP7"},
    {0x60, "KP8"},    {0x61, "KP9"},   {0x62, "KP0"},   {0x63, "KP."},
    {0xE0, "LCTL"},   {0xE1, "LSHFT"}, {0xE2, "LALT"},  {0xE3, "LGUI"},
    {0xE4, "RCTL"},   {0xE5, "RSHFT"}, {0xE6, "RALT"},  {0xE7, "RGUI"},
};

/* Static text buffer — lv_label_set_text_static() does NOT copy. */
static char showkey_buf[16];

static const char *lookup_key_name(uint16_t usage_page, uint32_t keycode)
{
    uint32_t u = keycode;

    if (usage_page != HID_USAGE_KEY)
    {
        return NULL;
    }
    if (u >= 0x04 && u <= 0x1D) /* letters A-Z */
    {
        showkey_buf[0] = (char)('A' + (u - 0x04));
        showkey_buf[1] = '\0';
        return showkey_buf;
    }
    if (u >= 0x1E && u <= 0x27) /* digits 1-0 */
    {
        static const char digits[] = "1234567890";
        showkey_buf[0] = digits[(u - 0x1E) % 10];
        showkey_buf[1] = '\0';
        return showkey_buf;
    }
    if (u >= 0x3A && u <= 0x45) /* F1-F12 */
    {
        snprintf(showkey_buf, sizeof(showkey_buf), "F%u", (unsigned)(u - 0x3A + 1));
        return showkey_buf;
    }
    for (size_t i = 0; i < ARRAY_SIZE(key_names); i++)
    {
        if (key_names[i].usage == u)
        {
            return key_names[i].name;
        }
    }
    return NULL;
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

static void showkey_status_update_cb(struct showkey_status_state state)
{
    struct zmk_widget_showkey_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        if (state.pressed)
        {
            const char *name = lookup_key_name(state.usage_page, state.keycode);
            lv_label_set_text_static(widget->label, name != NULL ? name : "KEY");
            lv_obj_set_style_text_color(widget->label, lv_color_hex(0xef4d43), LV_PART_MAIN);
        }
        else
        {
            lv_label_set_text_static(widget->label, "-");
            lv_obj_set_style_text_color(widget->label, lv_color_hex(0xececef), LV_PART_MAIN);
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
    lv_obj_set_size(widget->obj, 108, 82);
    lv_obj_set_pos(widget->obj, 66, 138);
#endif

    /* Mono_20 (full ASCII) — NerdFonts_20 has only PUA icons, no letters. */
    widget->label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->label, &Mono_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->label, lv_color_hex(0xececef), LV_PART_MAIN);
    lv_label_set_text_static(widget->label, "-");
    lv_obj_align(widget->label, LV_ALIGN_CENTER, 0, 0);

    sys_slist_append(&widgets, &widget->node);

    widget_showkey_status_init();
    return 0;
}

lv_obj_t *zmk_widget_showkey_status_obj(struct zmk_widget_showkey_status *widget)
{
    return widget->obj;
}
