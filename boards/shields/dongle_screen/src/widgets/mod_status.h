#pragma once

#include <lvgl.h>
#include <zmk/display.h>

struct zmk_widget_mod_status
{
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *shift;
    lv_obj_t *ctrl;
    lv_obj_t *alt;
    lv_obj_t *gui;
    lv_obj_t *shift_icon;
    lv_obj_t *ctrl_icon;
    lv_obj_t *alt_icon;
    lv_obj_t *gui_icon;
};

int zmk_widget_mod_status_init(struct zmk_widget_mod_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_mod_status_obj(struct zmk_widget_mod_status *widget);
