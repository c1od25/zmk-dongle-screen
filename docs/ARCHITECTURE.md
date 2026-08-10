# Dongle Display 架构解读（ErgoAstra 定制分支）

本文档解读 `feat/ergoastra-sleep-accent-fade` 分支（相对上游 +11.7k/-1.9k 行）的显示代码架构。分支深度定制了主题渐变、电池滤波、按键显示等模块。

> 本文档基于对 `boards/shields/dongle_screen/` 源码的通读整理，适合在修改显示代码前快速理解整体结构与踩坑点。

## 目录

- [仓库本质](#仓库本质)
- [架构分层](#架构分层)
- [核心机制](#核心机制)
- [Widget 速查表](#widget-速查表)
- [踩坑重点](#踩坑重点)

## 仓库本质

- **形态**：一个 Zephyr module（`zephyr/module.yml`: `board_root: .` + `depends: [lvgl]`），以 **shield** 形式接入 ZMK dongle 构建。
- **入口**：`zmk_display_status_screen()`（`custom_status_screen.c`）——ZMK display 层唯一调用点。
- **规模**：~2900 行核心 C + 8 个字体 + 1 个内嵌 GIF。
- **硬件**：ST7789V（SPI3 + MIPI-DBI 4 线，240x280，`y-offset=20` 显示 240x240 可视区）、PWM1 背光（反相）、APDS9960 环境光传感器。

## 架构分层

```
硬件层     ST7789V (SPI3+MIPI-DBI 4线, 240x280, y-offset=20)
           PWM1 → pwm-leds disp_bl (背光, 反相) | I2C → APDS9960 环境光
显示驱动    drivers/display/display_st7789v.c (MADCTL 旋转: NORMAL/90/180/270)
LVGL       RGB565/16bit, 双VDB, flush线程, 16KB mem pool, DPI 261
屏幕装配    custom_status_screen.c → zmk_display_status_screen() (ZMK唯一入口)
widgets    src/widgets/ 下 9 个 widget: battery/sleep/output/layer/mod/showkey/wpm/scanner/gif
主题       src/theme.c → 双半区睡眠时 强调色 红→青 2s 渐变
亮度       src/brightness.c → fade_thread/idle_thread/ambient_thread 三线程
```

### 代码目录

```
boards/shields/dongle_screen/
├── src/
│   ├── custom_status_screen.c   屏幕装配（widget 实例化 + Z-order）
│   ├── theme.c                  睡眠态强调色渐变（ErgoAstra 定制）
│   ├── brightness.c             背光/亮度/屏幕睡眠状态机（3 线程）
│   ├── screen_rotate_init.c     SYS_INIT 阶段设显示方向
│   ├── fonts/                   8 个 LVGL 字体 C 文件
│   ├── gifs/                    内嵌 GIF 数据
│   └── widgets/                 9 个显示 widget
├── include/
│   ├── theme.h                  主题 API 契约
│   ├── fonts.h                  字体声明
│   └── sleep_status.h           睡眠 widget 头（注意：唯一在 include/ 的 widget 头）
├── boards/                      nice_nano / xiao_ble 硬件接线 overlay
├── dongle_screen.overlay        chosen/zephyr,display
├── dongle_screen.conf           ZMK_DISPLAY 等 5 项配置
├── Kconfig.shield / Kconfig.defconfig   DONGLE_SCREEN_* 全部配置项
└── CMakeLists.txt               widget 按 Kconfig 条件编译
```

## 核心机制

### 1. 事件 → 显示 的数据流（线程安全铁律）

```
ZMK 事件(系统线程) → ZMK_DISPLAY_WIDGET_LISTENER 宏
  → ① 加锁写 __state (state_func 从事件提取/主动查询状态)
  → ② k_work_submit_to_queue(zmk_display_work_q)  → 显示线程
  → ③ work_cb → update_cb → LVGL 操作
```

**铁律**：LVGL 只能在显示线程操作；事件回调只写共享状态、绝不碰 LVGL。theme.c 用手写 `ZMK_LISTENER + k_work` 实现同样机制（事件回调只更新 `last_levels[]`，提交 `theme_work` 到显示队列再算睡眠状态）。

事件型 widget 用宏模板化；轮询/动画型 widget 用 `lv_timer`（天然在显示线程跑）。

### 2. 两种"睡眠"（极易混淆）

| | 屏幕睡眠 | 键盘睡眠 |
|---|---|---|
| 位置 | brightness.c | sleep_status.c + theme.c |
| 判定 | idle 超时 / F22 手动 | 双半区电池 level < 1 |
| 表现 | 背光渐变到 0 | wifi→leaf 图标 + 红色→青色渐变 |
| 状态 | `screen_on` + `off_through_modifier` | `last_levels[2]` 事件 + 1s 轮询兜底 |

两者互不感知——键盘睡眠不等于屏幕关闭。

### 3. 电池数据链路（battery_status.c 最复杂）

```
事件/轮询 → get_state → set_battery_symbol → apply_filter
  → EMA(α=0.35, scale=256) + 步进限幅(±10) + 尖峰拒绝(±45)
  → 1s 显示缓冲 (pending_timer 一次性计时器, 合并突发更新)
  → battery_display_render
断连(level<1%) → 立即渲染红X (跳过缓冲)
重连(<1→≥1)   → 直通原始值 + brightness_wake_screen_on_reconnect()
```

**关键洞察**：battery 是唯一的"连接状态传感器"——重连唤醒、键盘睡眠判定、"L"/"R" 标签全部依赖电池上报数值（ZMK 以电池 level<1 作为外设断开/睡眠信号），而非 BLE 连接事件。槽位是**连接顺序**（slot0=先配对），不是物理左右。

### 4. Widget 定位契约（新增 widget 易错点）

- **自定位**：layer / battery / showkey / wpm / scanner / gif / sleep（init 内 `lv_obj_set_pos`）
- **screen 代定位**：output（仅 `lv_obj_align(TOP_MID, 0, 10)`）+ **mod**（尺寸在 widget、位置在 screen `lv_obj_set_pos(60,50)/(66,50)`，最坑）

新增 widget 必须二选一定位模式，否则错位。

### 5. 亮度控制（brightness.c 三线程）

```
fade_thread(p6, 768B):    k_msgq 消费渐变请求; 先 k_msgq_purge 保证只执行最新目标
                          cubic ease-in-out 插值, 6-32 步, 500-1000ms
screen_idle_thread(p7, 512B): idle 超时 → 背光渐灭 → k_sleep(K_FOREVER) 等 k_wakeup
ambient_thread(p7, 512B):  APDS9960 光感 → 亮度映射 (CONFIG_DONGLE_SCREEN_AMBIENT_LIGHT)
key_listener:             F22/23/24 亮度键; 任何按键/层切换都刷新 last_activity 并唤醒
off_through_modifier:     区分"手动关" vs "空闲关" — 手动关后 idle 不二次关、也不自动唤醒
```

所有亮度变化必须走 `fade_to_brightness`（内部 purge 队列）；直接 `apply_brightness` 会打断动画且无边界检查。

## Widget 速查表

| Widget | 数据源 | 显示内容 | 关键机制/坑 |
|---|---|---|---|
| battery_status | 事件(peripheral/central battery + usb) + 1000ms 轮询 | 双槽垂直电量条 + NN% + L/R tag；断连红 X | 1s 显示缓冲；EMA+步进+尖峰滤波；重连直通+唤醒屏幕；槽位=连接顺序 |
| sleep_status | 事件 + 1000ms 轮询 | wifi(醒)/leaf(睡) 图标, accent 色 | 中央缓存 {0,0} 初始值坑（UNKNOWN 哨兵）；头文件在 include/ |
| output_status | 事件(endpoint + usb_conn) | usb_port 图标三色(accent/中灰/暗灰) | 无定时器；theme refresh 用 get_state(NULL) 重读 |
| layer_status | 事件(layer_state_changed) | 层名(Mono_20), 默认层浅色/其他 accent | 层图标 U+EBD2 缺字 → 纯文本 fallback |
| mod_status | **无事件**, 100ms lv_timer 轮询 HID report | 4 个修饰键块 + NerdFonts_40 图标 | 最特殊：无 listener/无 sys_slist/无 theme_register_refresh；激活色即时取 accent |
| scanner_status | 纯动画, 60ms lv_timer | 10 块扫描 + 6 级指数拖尾 | 无 alpha 混合(向背景 RGB 插值)；last_state diff 减重绘；46 帧状态机；与 WPM/GIF 互斥 |
| showkey_status | 事件(keycode_state_changed) | 键名(Mono_48) 或图标(NerdFonts_48, 修饰键带 L/R) | **跨半区 shift 读 HID report**；800ms hold + 400ms fade；静态 16B 缓冲 |
| gif_status | 无(lv_gif 内部定时器) | 70x70 内嵌 GIF | 横屏 Y=-7 裁剪；gif_data 精确 sizeof；与 WPM/scanner 互斥 |
| wpm_status | 事件(wpm_state_changed) | 速度图标(U+F04C5) + Mono_28 数值 | 静态 buf；`\U000F04C5` 8 位转义；无 theme refresh |

### 共用模式

1. **`ZMK_DISPLAY_WIDGET_LISTENER` + `ZMK_SUBSCRIPTION`**：宏生成 K_MUTEX 保护的共享 state——`refresh_state()` 在系统 workqueue 写，`work_cb` 提交到 `zmk_display_work_q()` 执行 LVGL。init 末尾必须调用生成的 `<name>_init()` 完成首次渲染。
2. **`theme_register_refresh`**：battery/sleep/output/layer/showkey/scanner 注册（**mod/wpm 未注册**）。accent 色元素必须注册才能在红→青渐变过程中逐帧换色。
3. **`lv_timer` 全在显示线程**：用 lv_timer 而非 k_timer（k_timer 跑系统 workqueue，改 LVGL 样式不安全）。
4. **`lv_label_set_text_static` + 模块级静态缓冲**：所有动态文本（电量%、WPM、showkey、层名）用 static 缓冲，绝不动态分配。
5. **字体最小字形集**：NerdFonts 各尺寸只含用到的 PUA 码点（`--symbols` 指定）。新图标必须加进对应字体再重新生成。
6. **sys_slist widgets 链表**：事件型 widget 广播更新用 `SYS_SLIST_FOR_EACH_CONTAINER`；mod/scanner/gif 不入链。
7. **Kconfig 条件编译**：`CONFIG_DONGLE_SCREEN_*_ACTIVE` 逐个开关；**WPM/SCANNER/GIF 三选一互斥**（`depends on` 链）。

## 踩坑重点

1. **`lv_timer_create` 默认无限循环**（`repeat_count=-1`）——一次性定时器必须 `lv_timer_set_repeat_count(timer, 1)`，否则僵尸 timer 持续触发、指针比较误路由到另一槽位（曾导致右半区电池立即显示的 bug）。
2. **`lv_timer_t` 是不完整类型**——不能用 `timer->user_data` 直接访问；用 `lv_timer_get_user_data(timer)`（创建时把槽位索引作为 user_data 传入）。
3. **showkey 跨半区 shift**——必须读 `zmk_hid_get_keyboard_report()->body.modifiers`；keycode 事件自带的 `implicit/explicit_modifiers` 只含绑定内嵌的 mod（如 `LS(SEMI)`），无法感知另一半半区按下的 Shift。
4. **scanner 无 alpha**——LVGL lv_obj 背景不透明，拖尾用向背景 `0x0a0a0d` 的 RGB 插值模拟透明度；改背景色需同步改 `SCAN_BG_R/G/B`。
5. **字体缺字形**——电量 glyph U+F240-F244 不在字体（用文本 "NN%"）；层图标 U+EBD2 缺失（纯文本）；BMP 以上码点必须 `\U0000XXXX` 8 位转义。
6. **sleep_status.h 位置不一致**——头在 `include/`，其余 widget 头在 `widgets/`。
7. **battery 滤波链路**——尖峰拒绝→EMA→步进限幅防跳动；重连直通防爬升；断连跳过缓冲立即渲染。
8. **线程安全纪律**——LVGL 操作只在显示线程（listener/timer/work-queue）；Zephyr 线程（fade/idle/ambient）只碰 `led_set_brightness` 和 `k_msgq`/`k_wakeup`。
9. **`DONGLE_SCREEN_SYSTEM_ICON` 是死配置**——已定义但代码未引用。

## 参考

- 上游 README：[`/README.md`](../README.md)（项目介绍、构建、接线）
- 接线指南：`docs/nice_nano_wire_guide.md`
- ZMK 显示 widget 宏：`<zmk/display.h>` 中 `ZMK_DISPLAY_WIDGET_LISTENER`
- ZMK 事件机制：`<zmk/event_manager.h>` 中 `ZMK_LISTENER` / `ZMK_SUBSCRIPTION`
