# AGENTS.md — boards/shields/dongle_screen（构建中枢）

**Shield 定义目录**。Kconfig.shield 一行 `shields_list_contains` 注册 shield；Kconfig.defconfig 承载全部 DONGLE_SCREEN_* 配置；CMakeLists 按 *_ACTIVE 条件编译 widget 源。父目录 AGENTS.md 已覆盖 widget 模式、lv_timer 坑、线程安全、定位契约，这里只讲本目录的构建配置知识。

## STRUCTURE

- `src/` — 核心：brightness.c、custom_status_screen.c（布局）、screen_rotate_init.c、theme.c
- `src/widgets/` — 9 个 widget 源，wpm/scanner/gif/sleep 按 *_ACTIVE 条件编译，其余无条件
- `src/fonts/` — 12 个 LVGL 字体 C 文件（Mono 正体+斜体、NerdFonts、Speedo、Gripper、Volume_48），`file(GLOB)` 无条件编译
- `src/gifs/` — GIF 数据，仅 `CONFIG_DONGLE_SCREEN_GIF_ACTIVE` 时 glob 编译
- `include/` — theme.h、fonts.h、sleep_status.h（唯一不在 widgets/ 的 widget 头）
- `boards/` — nice_nano 与 xiao_ble 两套硬件 overlay（接线差异见下）

## 关键配置（Kconfig.defconfig）

- **WPM/SCANNER/GIF 三选一互斥链**：SCANNER `depends on !WPM`，GIF `depends on !WPM && !SCANNER`。三者共用同一屏幕格子，默认全 n
- **HORIZONTAL 默认 y，FLIPPED 默认 n**。注意 `#ifdef` vs `#if` 用法不一致（见父文档 ANTI-PATTERNS）
- **亮度体系**：MAX_BRIGHTNESS（1-100，默认 80）/ MIN_BRIGHTNESS（1-99，默认 1）/ DEFAULT（默认 = MAX，range 收在 MIN..MAX）
- **键盘控亮度**：默认 y；UP=115(F24)、DOWN=114(F23)、TOGGLE=113(F22)、STEP=10
- **环境光**：AMBIENT_LIGHT 默认 n，`select SENSOR + APDS9960`；AMBIENT_LIGHT_TEST 默认 n（mock 传感器）；EVALUATION_INTERVAL_MS=1000；MIN_RAW=0 / MAX_RAW=100
- **SYSTEM_ICON 是死配置**（0/1/2 = macOS/Linux/Windows），定义但未使用，别改
- 其余默认 y：MODIFIER / LAYER / OUTPUT / BATTERY / SHOWKEY / SLEEP
- LVGL 内存体系：MEM_POOL=16384、VDB=50、DOUBLE_VDB=y、FLUSH_THREAD=y、COLOR_DEPTH 16 + SWAP、DPI=261、FONT_DEFAULT_MONTSERRAT_20

## 条件编译三层一致（改一处必同步另两层）

1. `Kconfig.defconfig` — 定义符号与互斥 `depends on` 链
2. `CMakeLists.txt` — `if(CONFIG_DONGLE_SCREEN_*_ACTIVE)` 决定源文件是否进编译
3. `src/custom_status_screen.c` — `#if`/`#ifdef` 决定 widget 是否注册初始化

新增 widget = 三层同步改。GIF 例外：多一层 `file(GLOB src/gifs/*.c)`。
无条件编译基座：brightness / custom_status_screen / screen_rotate_init / theme / output / battery / layer / mod / showkey。条件编译的只有 wpm / scanner / gif / sleep。

## dongle_screen.conf（仅 5 项）

- `ZMK_DISPLAY=y`、`ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y`、`ZMK_DISPLAY_DEDICATED_THREAD_STACK_SIZE=4096`
- `ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`
- `ZMK_DONGLE_DISPLAY_DONGLE_BATTERY=n` — 默认 n；Kconfig 定义处 `depends on BT && (!ZMK_SPLIT_BLE || ZMK_SPLIT_ROLE_CENTRAL)`
- **注意**：`CONFIG_ZMK_HID_INDICATORS=y`（caps lock 图标/showkey 大小写依赖）由 **consumer 的 dongle conf**（ergoastra `ergoastra_v1_ble_dongle.conf`）设置，不在本 shield conf

## dongle_screen.overlay（仅 chosen）

- 只做 `chosen { zephyr,display = &st7789; }`。全部硬件接线在 boards/ overlay，不在这里

## boards/ 接线差异（nice_nano vs xiao_ble）

| 项 | nice_nano | xiao_ble |
|----|-----------|----------|
| SPI3 MISO | P1.11 | P1.10 |
| CS | gpio1.6 | xiao_d 9 |
| DC | gpio1.4 | xiao_d 7 |
| RST | gpio0.11 | xiao_d 3 |
| 背光 PWM（pwm1，invert） | P0.10 | P1.11 |
| APDS9960 | i2c0，INT=gpio1.0 | i2c1，INT=xiao_d 2 |

两板共同点：都走 spi3（并禁用 spi2）、pwm1、同一份 ST7789V 面板参数（240x280、y-offset 20）。
相关但不在此目录：`drivers/display/` 用 `zephyr_library_amend` + `HEADER_FILE_ONLY` 掉包内置 ST7789V，改动会影响本 shield 的显示输出。
