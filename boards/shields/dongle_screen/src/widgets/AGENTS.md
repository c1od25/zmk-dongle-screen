# AGENTS.md — widgets/（9 个 LVGL widget）

## OVERVIEW
9 个显示 widget，统一 `zmk_widget_<name>_init` / `_obj` 接口。条件编译、定位契约、lv_timer 通用坑、线程安全、theme 注册机制已由父文档覆盖；本文件只讲三种驱动模式与各 widget 的特有机。

## 三种驱动模式
1. **事件驱动**（6/9）：`ZMK_DISPLAY_WIDGET_LISTENER` + `ZMK_SUBSCRIPTION`，ZMK 事件回调 → 更新 LVGL。battery / layer / output / showkey / sleep / wpm
2. **轮询驱动**：lv_timer 直接读 ZMK 状态。mod_status 100ms 读 HID report（主驱动）；battery / sleep 各加 1s 轮询 central 缓存兜底（补偿丢事件，仅值变化才更新）
3. **纯动画**：无事件。scanner_status 60ms lv_timer 推帧；gif_status 靠 lv_gif 内部 timer，widget 侧零代码

## 速查表
| widget | 驱动 | 数据源 | theme_register_refresh |
|--------|------|--------|------------------------|
| battery | 事件 + 1s 轮询 | battery 事件 + central 缓存 | ✓ |
| sleep | 事件 + 1s 轮询 | peripheral battery 事件 + central 缓存 | ✓ |
| output | 事件 | zmk_endpoint_changed / usb_conn_state_changed | ✓ |
| layer | 事件 | zmk_layer_state_changed + keymap 查询 | ✓ |
| showkey | 事件 | zmk_keycode_state_changed | ✓ |
| wpm | 事件 | zmk_wpm_state_changed | ✗ |
| mod | 100ms 轮询 | zmk_hid_get_keyboard_report() | ✗ |
| scanner | 60ms 动画 | 无（自推帧） | ✓ |
| gif | lv_gif 内部 | 无（嵌入字节流） | ✗ |

## battery_status 特殊机制（最复杂）
- **EMA 滤波**：定点 scale=256，EMA_ALPHA_FP=90（≈0.35），避免浮点运算
- **步进限幅**：STEP_LIMIT=10，单次更新最多跳 10%，防唤醒跳变
- **尖峰拒绝**：SPIKE_THRESHOLD=45，单样本跳变 >45% 丢弃（120s 报告间隔下真实电池不可能）
- **1s 显示缓冲**：滤波值先入 pending，每 slot 一个 one-shot lv_timer（1000ms），每次更新 lv_timer_reset 续命，timer 触发才渲染 → 快速连续更新合并为一次刷新，防闪烁
- **断连立即渲染**：level<1 时删 pending timer 直接渲染（空条 + 红 "X" 标签），不等缓冲
- **重连检测**：last<1 && new≥1 → 原始值直通（跳过 EMA 斜坡，否则 120s 间隔要几分钟爬回）+ just_reconnected 标记 + `brightness_wake_screen_on_reconnect()` 唤屏桥接
- slot = 连接顺序，"L"/"R" 是序号非物理边；usb_present 字段是死代码

## showkey_status 特殊机制
- **跨半区 shift 必须读 HID report**：`zmk_hid_get_keyboard_report()->body.modifiers`（central 汇总所有半区，与宿主收到的字节一致）。keycode 事件自带的 implicit/explicit_modifiers 只描述该键绑定内嵌的 mod（如 LS(SEMI)），看不到另一半按的 Shift，不可用
- **时序**：按下即显 → 松开后 800ms one-shot hold timer → 400ms lv_anim 淡出（255→0）→ 清文本。按下期间先删旧 timer + lv_anim_delete 防叠加
- SHOWKEY_SIDE_ICON（L/R 前缀 + 图标）用 lv_text_get_width 实测两段宽度居中

## mod_status 最特殊
- 无 listener / 无 sys_slist / 无 theme_register_refresh，纯 100ms lv_timer 轮询 `zmk_hid_get_keyboard_report()->body.modifiers`，按 MOD_LSFT|RSFT 等位掩码刷新 4 键亮灭
- 唯一"只定尺寸、由 screen 文件定位"的 widget

## 约定
- **静态缓冲**：`lv_label_set_text_static` 不拷贝，文本一律进模块级 static 数组（battery_objects[].text、layer_index_text[4]、showkey_buf[16]、buf[8]），绝不用局部变量
- **字体二分**：文本用 Mono_*（20/28/48），图标用 NerdFonts_*（Regular_20/28/40/48、Speedo_40）PUA 码点
- **码点纪律**：图标码点必须在字体 --symbols 里；BMP 以上必须 `\U0000XXXX` 8 位转义（如 U+F04C5），4 位 \uXXXX 渲染豆腐块
- **缺字形**：battery U+F240-F244、layer U+EBD2 不在提交字体 → 前者退化为百分比文本，后者退化为纯文本
- **sleep_status.h 在 include/ 不在本目录**（唯一例外）

## 注意
- wpm / scanner / gif 三选一互斥共用一格（Kconfig.defconfig depends on 链）
- scanner 拖尾色向背景 0x1a1b26 渐变（lv_obj 背景不透明、无 alpha 混合），改面板背景需同步 SCAN_BG_*
- gif 是 70x70 嵌入数组（55 帧 @40ms，47610B），portrait 下 GIF_Y=-7 上下裁 7px；gif_data 声明带精确尺寸使 sizeof 是常量
- layer_status.c 用 `#ifdef` 查 HORIZONTAL（其余用 `#if`），新代码一律 `#if`
