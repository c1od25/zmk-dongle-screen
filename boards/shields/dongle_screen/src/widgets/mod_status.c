#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/hid.h>
#include <lvgl.h>
#include "mod_status.h"
#include <fonts.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Design palette (design doc §4). */
#define MOD_BG_IDLE       0x121216
#define MOD_BORDER_IDLE   0x2a2a34
#define MOD_TEXT_IDLE     0x383842
#define MOD_BG_ACTIVE     0x241412
#define MOD_BORDER_ACTIVE 0xe8453c
#define MOD_TEXT_ACTIVE   0xe8453c

/* Mods cell geometry (design doc §2.5 portrait / §3.5 landscape). */
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
#define MOD_CELL_W 200
#define MOD_CELL_H 56
#define MOD_KEY_W  92
#define MOD_KEY_H  25
#else
#define MOD_CELL_W 108
#define MOD_CELL_H 82
#define MOD_KEY_W  52
#define MOD_KEY_H  30
#endif

/* Key positions relative to the mods cell origin. */
static const lv_point_t mod_key_pos[4] = {
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
    {5, 0},   /* Shift */
    {103, 0}, /* Ctrl  */
    {5, 31},  /* Alt   */
    {103, 31} /* GUI   */
#else
    {0, 9},  /* Shift */
    {56, 9}, /* Ctrl  */
    {0, 43},  /* Alt   */
    {56, 43}  /* GUI   */
#endif
};

/* Nerd Font PUA mod symbols (PRESENT in NerdFonts_Regular_20/40). */
#define MOD_ICON_SHIFT "\U000F0636" /* nf-md-apple_keyboard_shift   U+F0636 */
#define MOD_ICON_CTRL  "\U000F0634" /* nf-md-apple_keyboard_control U+F0634 */
#define MOD_ICON_ALT   "\U000F0635" /* nf-md-apple_keyboard_option  U+F0635 */
#define MOD_ICON_GUI   "\U000F0633" /* nf-md-apple_keyboard_command U+F0633 */

static void set_mod_key_active(lv_obj_t *key, bool active)
{
    lv_obj_set_style_bg_color(key, lv_color_hex(active ? MOD_BG_ACTIVE : MOD_BG_IDLE), LV_PART_MAIN);
    lv_obj_set_style_border_color(key,
                                  lv_color_hex(active ? MOD_BORDER_ACTIVE : MOD_BORDER_IDLE),
                                  LV_PART_MAIN);
    lv_obj_set_style_text_color(key, lv_color_hex(active ? MOD_TEXT_ACTIVE : MOD_TEXT_IDLE),
                                LV_PART_MAIN);
}

static lv_obj_t *mod_key_create(lv_obj_t *parent, const lv_point_t *pos, const char *icon,
                                const char *name)
{
    lv_obj_t *key = lv_obj_create(parent);
    lv_obj_set_pos(key, pos->x, pos->y);
    lv_obj_set_size(key, MOD_KEY_W, MOD_KEY_H);
    lv_obj_set_style_pad_top(key, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(key, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(key, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(key, 0, LV_PART_MAIN);
    lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);
    set_mod_key_active(key, false);

    lv_obj_t *mk_icon = lv_label_create(key);
    lv_label_set_text(mk_icon, icon);
    lv_obj_set_style_text_font(mk_icon, &NerdFonts_Regular_20, LV_PART_MAIN);
    lv_obj_align(mk_icon, LV_ALIGN_CENTER, -18, 0);

    lv_obj_t *mk_name = lv_label_create(key);
    lv_label_set_text(mk_name, name);
    lv_obj_set_style_text_font(mk_name, &Mono_12, LV_PART_MAIN);
    lv_obj_align_to(mk_name, mk_icon, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    return key;
}

static void update_mod_status(struct zmk_widget_mod_status *widget)
{
    uint8_t mods = zmk_hid_get_keyboard_report()->body.modifiers;

    set_mod_key_active(widget->shift, mods & (MOD_LSFT | MOD_RSFT));
    set_mod_key_active(widget->ctrl, mods & (MOD_LCTL | MOD_RCTL));
    set_mod_key_active(widget->alt, mods & (MOD_LALT | MOD_RALT));
    set_mod_key_active(widget->gui, mods & (MOD_LGUI | MOD_RGUI));
}

static void mod_status_timer_cb(struct k_timer *timer)
{
    struct zmk_widget_mod_status *widget = k_timer_user_data_get(timer);
    update_mod_status(widget);
}

static struct k_timer mod_status_timer;

int zmk_widget_mod_status_init(struct zmk_widget_mod_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, MOD_CELL_W, MOD_CELL_H);
    lv_obj_set_style_pad_top(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(widget->obj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    widget->shift = mod_key_create(widget->obj, &mod_key_pos[0], MOD_ICON_SHIFT, "Shift");
    widget->ctrl = mod_key_create(widget->obj, &mod_key_pos[1], MOD_ICON_CTRL, "Ctrl");
    widget->alt = mod_key_create(widget->obj, &mod_key_pos[2], MOD_ICON_ALT, "Alt");
    widget->gui = mod_key_create(widget->obj, &mod_key_pos[3], MOD_ICON_GUI, "GUI");

    k_timer_init(&mod_status_timer, mod_status_timer_cb, NULL);
    k_timer_user_data_set(&mod_status_timer, widget);
    k_timer_start(&mod_status_timer, K_MSEC(100), K_MSEC(100));

    return 0;
}

lv_obj_t *zmk_widget_mod_status_obj(struct zmk_widget_mod_status *widget)
{
    return widget->obj;
}
