#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/hid.h>
#include <lvgl.h>
#include "mod_status.h"
#include <fonts.h>
#include <theme.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Design palette (design doc §4). */
#define MOD_BG_IDLE       0x16161e
#define MOD_BORDER_IDLE   0x3b4261
#define MOD_TEXT_IDLE     0x737aa2

/* Mods cell geometry (design doc §2.5 portrait / §3.5 landscape). */
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
#define MOD_CELL_W 200
#define MOD_CELL_H 56
#define MOD_KEY_W  50
#define MOD_KEY_H  48
#else
#define MOD_CELL_W 108
#define MOD_CELL_H 82
#define MOD_KEY_W  52
#define MOD_KEY_H  38
#endif

/* Key positions relative to the mods cell origin. */
static const lv_point_t mod_key_pos[4] = {
#if CONFIG_DONGLE_SCREEN_HORIZONTAL
    {0, 4},   /* Shift */
    {50, 4},  /* Ctrl  */
    {100, 4}, /* Alt   */
    {150, 4}  /* GUI   */
#else
    {0, 1},  /* Shift */
    {56, 1}, /* Ctrl  */
    {0, 43},  /* Alt   */
    {56, 43}  /* GUI   */
#endif
};

/* Nerd Font PUA mod symbols (PRESENT in NerdFonts_Regular_40). */
#define MOD_ICON_SHIFT "\U000F0636" /* nf-md-apple_keyboard_shift   U+F0636 */
#define MOD_ICON_CTRL  "\U000F0634" /* nf-md-apple_keyboard_control U+F0634 */
#define MOD_ICON_ALT   "\U000F0635" /* nf-md-apple_keyboard_option  U+F0635 */
#define MOD_ICON_GUI   "\U000F0633" /* nf-md-apple_keyboard_command U+F0633 */

static void set_mod_key_active(lv_obj_t *key, lv_obj_t *icon, bool active)
{
    if (active)
    {
        lv_obj_set_style_bg_color(key, lv_color_darken(theme_accent_color(), 190),
                                  LV_PART_MAIN);
        lv_obj_set_style_border_color(key, theme_accent_color(), LV_PART_MAIN);
        lv_obj_set_style_text_color(key, theme_accent_color(), LV_PART_MAIN);
        lv_obj_set_style_text_color(icon, theme_accent_color(), LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_bg_color(key, lv_color_hex(MOD_BG_IDLE), LV_PART_MAIN);
        lv_obj_set_style_border_color(key, lv_color_hex(MOD_BORDER_IDLE), LV_PART_MAIN);
        lv_obj_set_style_text_color(key, lv_color_hex(MOD_TEXT_IDLE), LV_PART_MAIN);
        lv_obj_set_style_text_color(icon, lv_color_hex(MOD_TEXT_IDLE), LV_PART_MAIN);
    }
}

static lv_obj_t *mod_key_create(lv_obj_t *parent, const lv_point_t *pos, const char *icon,
                                lv_obj_t **icon_out)
{
    lv_obj_t *key = lv_obj_create(parent);
    lv_obj_set_pos(key, pos->x, pos->y);
    lv_obj_set_size(key, MOD_KEY_W, MOD_KEY_H);
    lv_obj_set_style_pad_top(key, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(key, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(key, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(key, 0, LV_PART_MAIN);
    lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mk_icon = lv_label_create(key);
    lv_label_set_text(mk_icon, icon);
    /* 40px icon centered in both orientations (landscape 48px / portrait 38px
     * keys) — the mod glyphs are ~21px wide, so 50px landscape keys fit. */
    lv_obj_set_style_text_font(mk_icon, &NerdFonts_Regular_40, LV_PART_MAIN);
    lv_obj_align(mk_icon, LV_ALIGN_CENTER, 0, 0);
    if (icon_out != NULL)
    {
        *icon_out = mk_icon;
    }

    set_mod_key_active(key, mk_icon, false);

    return key;
}

static void update_mod_status(struct zmk_widget_mod_status *widget)
{
    uint8_t mods = zmk_hid_get_keyboard_report()->body.modifiers;

    set_mod_key_active(widget->shift, widget->shift_icon, mods & (MOD_LSFT | MOD_RSFT));
    set_mod_key_active(widget->ctrl, widget->ctrl_icon, mods & (MOD_LCTL | MOD_RCTL));
    set_mod_key_active(widget->alt, widget->alt_icon, mods & (MOD_LALT | MOD_RALT));
    set_mod_key_active(widget->gui, widget->gui_icon, mods & (MOD_LGUI | MOD_RGUI));
}

static void mod_status_timer_cb(lv_timer_t *timer)
{
    struct zmk_widget_mod_status *widget = lv_timer_get_user_data(timer);
    update_mod_status(widget);
}

int zmk_widget_mod_status_init(struct zmk_widget_mod_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, MOD_CELL_W, MOD_CELL_H);
    lv_obj_set_style_pad_top(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(widget->obj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    widget->shift = mod_key_create(widget->obj, &mod_key_pos[0], MOD_ICON_SHIFT, &widget->shift_icon);
    widget->ctrl = mod_key_create(widget->obj, &mod_key_pos[1], MOD_ICON_CTRL, &widget->ctrl_icon);
    widget->alt = mod_key_create(widget->obj, &mod_key_pos[2], MOD_ICON_ALT, &widget->alt_icon);
    widget->gui = mod_key_create(widget->obj, &mod_key_pos[3], MOD_ICON_GUI, &widget->gui_icon);

    /* LVGL timer runs inside lv_timer_handler() on the display thread — unlike a
     * Zephyr k_timer (system workqueue), it is thread-safe with LVGL styles. */
    lv_timer_create(mod_status_timer_cb, 100, widget);

    return 0;
}

lv_obj_t *zmk_widget_mod_status_obj(struct zmk_widget_mod_status *widget)
{
    return widget->obj;
}
