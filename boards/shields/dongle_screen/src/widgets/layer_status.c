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

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct layer_status_state
{
    uint8_t index;
    const char *label;
};

static char layer_index_text[4];

static void layer_status_update_cb(struct layer_status_state state)
{
    struct zmk_widget_layer_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node)
    {
        const char *label = state.label;

        if (label == NULL || label[0] == '\0')
        {
            snprintf(layer_index_text, sizeof(layer_index_text), "%u", state.index);
            label = layer_index_text;
        }

        lv_label_set_text_static(widget->layer_name, label);

        lv_obj_set_style_text_color(widget->layer_name,
                                    state.index > 0 ? lv_color_hex(0xef4d43)
                                                    : lv_color_hex(0xececef),
                                    LV_PART_MAIN);
    }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh)
{
    uint8_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index,
        .label = zmk_keymap_layer_name(index)};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

int zmk_widget_layer_status_init(struct zmk_widget_layer_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);

    lv_obj_set_size(widget->obj, 108, 22);
    lv_obj_set_pos(widget->obj, 66, 10);
#ifdef CONFIG_DONGLE_SCREEN_HORIZONTAL
    lv_obj_set_size(widget->obj, 200, 22);
    lv_obj_set_pos(widget->obj, 60, 10);
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
    lv_obj_set_style_text_font(widget->layer_name, &Mono_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->layer_name, lv_color_hex(0xececef), LV_PART_MAIN);
    lv_label_set_text_static(widget->layer_name, "");
    lv_obj_align(widget->layer_name, LV_ALIGN_CENTER, 0, 0);

    sys_slist_append(&widgets, &widget->node);

    widget_layer_status_init();
    return 0;
}

lv_obj_t *zmk_widget_layer_status_obj(struct zmk_widget_layer_status *widget)
{
    return widget->obj;
}
