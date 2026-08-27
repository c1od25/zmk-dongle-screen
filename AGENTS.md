# AGENTS.md — ZMK Dongle Screen (ErgoAstra fork)

**Generated:** 2026-08-10
**Branch:** feat/ergoastra-sleep-accent-fade

## OVERVIEW

Zephyr module + ZMK shield for a dongle display (ST7789V 240x280 + LVGL) on split-BLE keyboards. Fork of janpfischer/zmk-dongle-screen, deeply customized for ErgoAstra: 9 LVGL widgets, sleep-state accent fade, Tokyo Night palette, transport-based connection state, custom ST7789V driver.

## STRUCTURE

```
./
├── boards/shields/dongle_screen/  # The shield: widgets, theme, brightness, Kconfig
│   ├── src/                       # Core: theme.c, brightness.c, custom_status_screen.c
│   │   ├── widgets/               # 9 LVGL display widgets
│   │   ├── fonts/                 # 8 generated LVGL font C files (minimal glyph sets)
│   │   └── gifs/                  # Embedded GIF data
│   ├── boards/                    # nice_nano + xiao_ble hardware overlays
│   └── include/                   # theme.h, fonts.h, sleep_status.h (only header not in widgets/)
├── drivers/display/               # Custom ST7789V driver (overrides Zephyr built-in)
├── zephyr/module.yml              # Module declaration (board_root: ., depends: lvgl)
└── config/west.yml                # West manifest pinning ZMK main
```

## WHERE TO LOOK

| Task | Location |
|------|----------|
| Add/modify a display widget | `boards/shields/dongle_screen/src/widgets/` (see its AGENTS.md) |
| Sleep-state accent color fade | `boards/shields/dongle_screen/src/theme.c` |
| Backlight/brightness/screen sleep | `boards/shields/dongle_screen/src/brightness.c` |
| Screen layout / widget positions | `boards/shields/dongle_screen/src/custom_status_screen.c` |
| Build config / Kconfig toggles | `boards/shields/dongle_screen/Kconfig.defconfig` + `CMakeLists.txt` |
| LCD driver / rotation | `drivers/display/` |
| Battery filtering / 1s display buffer | `boards/shields/dongle_screen/src/widgets/battery_status.c` |

## CONVENTIONS (deviations from stock ZMK)

- **`ZMK_DISPLAY_WIDGET_LISTENER` + `ZMK_SUBSCRIPTION`** is the standard widget pattern (5 of 9 widgets). Exceptions: `battery_status` + `sleep_status` (1s poll timers), `mod_status` (100ms lv_timer polling HID report), `scanner_status` (60ms animation timer), `gif_status` (lv_gif internal timer).
- **Two-layer conditional compilation**: widget sources are gated by `CONFIG_DONGLE_SCREEN_*_ACTIVE` in BOTH `CMakeLists.txt` AND `custom_status_screen.c`. Change both or the build breaks.
- **WPM / SCANNER / GIF are mutually exclusive** (`depends on !` chain in Kconfig.defconfig) — they share one screen cell.
- **Widget positioning contract**: most widgets self-position in their `init`; `mod_status` is sized by the widget but positioned by the screen file. See "Positioning contract" comment in `custom_status_screen.c`.
- **Thread-safety**: LVGL ops only on the display thread (lv_timer / display work queue). ZMK event callbacks only write state, then `k_work_submit_to_queue(zmk_display_work_q(), ...)`.
- **Palette**: Tokyo Night night style (bg `#1a1b26`, fg `#c0caf5`, red `#f7768e`, cyan `#7dcfff`) — accents defined only in `include/theme.h`, all widgets use `theme_accent_color()`. See src/widgets/AGENTS.md.

## ANTI-PATTERNS (THIS PROJECT)

- **`lv_timer_create` defaults to infinite loop** (`repeat_count=-1`). One-shot timers MUST call `lv_timer_set_repeat_count(timer, 1)` or stale timers fire forever and get misrouted.
- **`lv_timer_t` is an incomplete type** — never access `timer->user_data`; use `lv_timer_get_user_data()`.
- **`lv_label_set_text_static()` does NOT copy** — always back with a module-static buffer (never a stack/local string).
- **`DONGLE_SCREEN_SYSTEM_ICON` is dead config** — defined but unused.
- **`#ifdef` vs `#if` inconsistency**: `layer_status.c` and `screen_rotate_init.c` use `#ifdef CONFIG_DONGLE_SCREEN_HORIZONTAL` while all others use `#if`.
- **Font glyphs are minimal sets** — new icon requires regenerating the font C file with the new PUA codepoint in `--symbols`; codepoints above BMP need `\U0000XXXX` (8-digit) escape. Codepoints verified against nerdfont.csv (e.g. `nf-md-caps_lock` = U+F0A9B).
- **scanner trail colors assume background 0x1a1b26** — if panel bg changes, update `SCAN_BG_R/G/B` in `scanner_status.c`.
- **Dark-bg contrast rule**: on a brightened bg, "darker" is capped at ~1.23:1 (black floor) — to distinguish elements they must be BRIGHTER than the bg. Rain LUT dim end uses `darken(accent, 120)` (was 200, invisible on #1a1b26).

## COMMANDS

```bash
# Build the dongle shield (from an app workspace that includes this module)
west build -b nice_nano@2//zmk -- -DSHIELD="<dongle> dongle_screen" -DZMK_EXTRA_MODULES=<path-to-this-repo>

# Local fork workflow: edit → commit → push to feat/ergoastra-sleep-accent-fade
# Then bump revision in the consumer repo's config/west.yml
```

## NOTES

- **Two kinds of "sleep"**: screen sleep (backlight off, in `brightness.c`) vs keyboard sleep (30s no-key-activity accent fade, in `sleep_status.c` + `theme.c`). They are independent.
- **Connection state comes from the transport, not battery events**: L/R tags are driven by `active_transport->api->get_available_source_ids()` in a 1s poll — battery events were unreliable on reboot (ADC not ready, dropped queue). See widgets/AGENTS.md.
- **Slot = connection order**, not physical side. Tag "L"/"R" are ordinals.
- **No CI in this repo** — builds are driven by the consumer workspace. This repo only ships west.yml + docs.
- **`sleep_status.h` lives in `include/`** not `widgets/` (only exception to the layout rule).
- Full design rationale: `docs/ARCHITECTURE.md` (158 lines, comprehensive gotchas).
