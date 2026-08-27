/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>

#include "layer_status.h"
#include <fonts.h>
#include <theme.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct layer_status_state
{
    uint8_t index;
    const char *label;
};

static char layer_index_text[4];

/* WIN-LOCK (2) / DIR (3) are toggle layers that stay active once engaged and
 * are represented by the top-bar gamepad/arrows icons. Skip them here so the
 * text cell shows the next active layer (BASE/FN) instead of being blanked by
 * the toggle layer covering the momentary-layer display. */
#define LAYER_INDEX_WINLOCK 2
#define LAYER_INDEX_DIR 3

static uint8_t display_layer_index(void)
{
    zmk_keymap_layers_state_t state = zmk_keymap_layer_state();

    for (int idx = ZMK_KEYMAP_LAYERS_LEN - 1; idx >= 0; idx--)
    {
        if (idx == LAYER_INDEX_WINLOCK || idx == LAYER_INDEX_DIR)
        {
            continue;
        }
        if (zmk_keymap_layer_active(idx))
        {
            return idx;
        }
    }
    return 0;
}

static void layer_status_update_cb(struct layer_status_state state)
{
    struct zmk_widget_layer_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        lv_obj_clear_flag(widget->layer_name, LV_OBJ_FLAG_HIDDEN);

        const char *label = state.label;

        if (label == NULL || label[0] == '\0')
        {
            snprintf(layer_index_text, sizeof(layer_index_text), "%u", state.index);
            label = layer_index_text;
        }

        lv_label_set_text_static(widget->layer_name, label);

        lv_obj_set_style_text_color(widget->layer_name,
                                    state.index > 0 ? theme_accent_color()
                                                    : lv_color_hex(0xc0caf5),
                                    LV_PART_MAIN);
    }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh)
{
    uint8_t index = display_layer_index();
    return (struct layer_status_state){
        .index = index,
        .label = zmk_keymap_layer_name(index)};
}

static void layer_status_refresh(void)
{
    layer_status_update_cb(layer_status_get_state(NULL));
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

int zmk_widget_layer_status_init(struct zmk_widget_layer_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);

    lv_obj_set_size(widget->obj, 108, 22);
    lv_obj_set_pos(widget->obj, 66, 15);
#ifdef CONFIG_DONGLE_SCREEN_HORIZONTAL
    lv_obj_set_size(widget->obj, 200, 22);
    lv_obj_set_pos(widget->obj, 60, 13);
#endif

    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(widget->obj, 0, LV_PART_MAIN);

    /* Layer icon U+EBD2 is missing from the committed fonts (design §5 audit), so the
     * documented fallback renders the layer name as plain text with no icon. */
    widget->layer_name = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->layer_name, &Mono_Italic_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->layer_name, lv_color_hex(0xc0caf5), LV_PART_MAIN);
    lv_label_set_text_static(widget->layer_name, "");
    lv_obj_align(widget->layer_name, LV_ALIGN_CENTER, 0, 0);

    sys_slist_append(&widgets, &widget->node);

    theme_register_refresh(layer_status_refresh);

    widget_layer_status_init();
    return 0;
}

lv_obj_t *zmk_widget_layer_status_obj(struct zmk_widget_layer_status *widget)
{
    return widget->obj;
}
