# AGENTS.md — src/（核心机制：theme / brightness / screen）

**核心机制目录**：睡眠态强调色渐变（theme.c）、背光三线程模型（brightness.c）、屏幕组装入口（custom_status_screen.c）、面板方向（screen_rotate_init.c）。widget 层见 widgets/AGENTS.md；Kconfig/条件编译三层体系见父目录 AGENTS.md，这里不重复。

## theme.c（头文件在 include/theme.h，不在本目录）

- **睡眠态强调色渐变**：无按键 30s（`SLEEP_ACTIVITY_TIMEOUT_MS`）→ 红 `THEME_ACCENT_RED`(0xf7768e, Tokyo Night red) 渐变为青 `THEME_ACCENT_CYAN`(0x7dcfff, Tokyo Night cyan)，THEME_FADE_MS=2000ms
- **配色=Tokyo Night night 风格**（bg `#1a1b26`/fg `#c0caf5`/red `#f7768e`/cyan `#7dcfff`），全部色值集中在 theme.h（`THEME_COLOR_*` + THEME_ACCENT_*），widget 不再写死十六进制；换主题只改 theme.h
- `theme_accent_color()` 返回当前插值色；所有渲染 accent 的 widget 必须换用此函数，别再写死色值
- `theme_register_refresh(cb)` 注册逐帧回调，上限 THEME_MAX_REFRESH=16，超限在 assert 构建中 `__ASSERT` 中止、否则 `LOG_ERR` 后丢弃（**不再静默**）
- **睡眠判定 = 按键活动超时**（`zmk_keycode_state_changed` 刷新 last_activity），**不依赖电池事件**（半区深睡已禁用，电池电平不再归零）；`theme_compute_asleep()` 由键活动时间戳计算
- 事件通道：ZMK_LISTENER(theme) 订阅 `zmk_keycode_state_changed` → `k_work_submit_to_queue(zmk_display_work_q(), ...)`；LVGL 只由 work 回调动（线程安全契约）
- **1s lv_timer 轮询兜底**：`theme_poll_cb` 检查无键活动超时，只在状态跳变时提 work
- **lv_color_mix 方向坑**：`lv_color_mix(c1=红, c2=青, v)`：v==0→青、v==255→红。故动画值反向——fade→青时 255→0，fade→红时 0→255（`theme_start_fade` 的 values 与直觉相反）
- `theme_init()`：初始色按当前睡眠态一步到位（无动画），启动 1000ms 轮询 timer；work 回调只在状态跳变时启动 fade，不重复动画

## custom_status_screen.c（屏幕组装）

- `zmk_display_status_screen()` 是 ZMK 唯一入口（ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y 时被核心调用），返回值即 screen
- **theme_init() 最先调用**（在创建根对象之前），保证各 widget init 时能读到正确 accent
- widget 按 `#if CONFIG_DONGLE_SCREEN_*_ACTIVE` 条件实例化（模块静态对象 + init 调用），与 CMakeLists 二层一致
- **Z-order = 创建序**：output→sleep→layer→topsep→battery→mod→showkey→wpm/scanner/gif，后建渲染在上；battery 面板透明仅两列，不遮挡下层
- 根背景 0x1a1b26 全不透明、零 padding；global_style = 白字 + letter/line space 1
- **定位契约**在文件头注释（71-95 行）逐 widget 列出：多数自定位；mod 只定尺寸由 screen 放 (60/66,50)；sleep 图标顶替 BT 格 (290,10)/(210,10)；wpm/scanner/gif 共用中下格
- 布局全部显式 pos/align，无 flex/grid；新增 widget 先看头注释再定位

## brightness.c（背光三线程模型）

- 三线程：fade_thread（p6, 768B）/ idle_thread（p7, 512B）/ ambient_thread（p7, 512B，仅 AMBIENT_LIGHT=y 时存在）
- **fade_to_brightness() 必须先 `k_msgq_purge` 再 put**：fade_msgq 容量 4，purge 保证只执行最新请求，过期渐变不排队堆积
- fade 动画：ease_in_out 三次缓动，steps=CLAMP(diff*2,6,32)，时长 CLAMP(diff*20,500,1000)ms；末帧兜底强制设目标值
- **off_through_modifier 区分两种关屏**：手动关（亮度键/toggle 置 true）vs 空闲自动关（置 false）。idle 线程仅在 `screen_on || off_through_modifier` 时计时；空闲关屏后重置 flag 并 k_sleep(K_FOREVER)
- 亮度键：UP=115(F24)、DOWN=114(F23)、TOGGLE=113(F22)，key_listener 只响应 key-down；亮度增减走 `brightness_modifier`，基础值不动
- `brightness_wake_screen_on_reconnect()` 由 battery widget 在重连时调用：唤屏 + 重置 last_activity + `k_wakeup(idle_tid)`
- 编译期 #error 校验 MIN/MAX/DEFAULT/MODIFIER 关系；改亮度配置先过这关
- 768B 栈是注释自认的 "guess"——改 fade 逻辑防栈溢出
- 环境光段风格混用：`#if`（133 行）与 `IS_ENABLED()`（525 行）并存，`#ifndef CONFIG_DONGLE_SCREEN_AMBIENT_LIGHT_TEST` 是 n 默认符号的正确写法

## screen_rotate_init.c（面板方向）

- `SYS_INIT(disp_set_orientation, APPLICATION, 60)`；`display_set_orientation` 写 ST7789V MADCTL
- HORIZONTAL × FLIPPED 四组合 → ROTATED_90 / 270 / NORMAL / 180
- 方向条件统一用 `#if`（已清理历史 `#ifdef`），与 custom_status_screen.c 一致

## 交叉要点

- **两义"睡眠"别混**：theme.c 是键盘睡眠（双半区电池<1）；brightness.c 是屏幕睡眠（idle 超时/手动关）。机制完全独立、互不触发
- 改动 theme 配色常量或回调上限，波及所有注册过 accent_refresh 的 widget（grep `theme_register_refresh` 定位）
