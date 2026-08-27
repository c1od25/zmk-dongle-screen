# AGENTS.md — widgets/（9 个 LVGL widget）

## OVERVIEW
9 个显示 widget，统一 `zmk_widget_<name>_init` / `_obj` 接口。条件编译、定位契约、lv_timer 通用坑、线程安全、theme 注册机制已由父文档覆盖；本文件只讲三种驱动模式与各 widget 的特有机。

## 三种驱动模式
1. **事件驱动**（5/9）：`ZMK_DISPLAY_WIDGET_LISTENER` + `ZMK_SUBSCRIPTION`，ZMK 事件回调 → 更新 LVGL。layer / output / showkey / wpm / gif
2. **轮询驱动**：lv_timer 直接读 ZMK 状态。battery + sleep 各 1s 轮询（读 transport 连接状态 + central 缓存）；mod_status 100ms 读 HID report
3. **纯动画**：无事件。scanner_status 60ms lv_timer 推帧；gif_status 靠 lv_gif 内部 timer，widget 侧零代码

## 速查表
| widget | 驱动 | 数据源 | theme_register_refresh |
|--------|------|--------|------------------------|
| battery | 1s 轮询 | transport 连接状态 + central 电池缓存 | ✓ |
| sleep | 1s 轮询 | theme_is_asleep()（键活动超时）| ✓ |
| output | 事件 | zmk_endpoint_changed / usb_conn_state_changed / hid_indicators_changed | ✓ |
| layer | 事件 | zmk_layer_state_changed + keymap 查询 | ✓ |
| showkey | 事件 | zmk_keycode_state_changed | ✓ |
| wpm | 事件 | zmk_wpm_state_changed | ✗ |
| mod | 100ms 轮询 | zmk_hid_get_keyboard_report() | ✗ |
| scanner | 60ms 动画 | 无（自推帧） | ✓ |
| gif | lv_gif 内部 | 无（嵌入字节流） | ✗ |

## battery_status 特殊机制（连接状态 = transport 轮询，最复杂）

**架构演进**：曾用电池事件判断连接（EMA 滤波/1s 缓冲/spike 拒绝/动画管线），boot 期不可靠 → 全部移除，改为**单一 1s 轮询 + 直接渲染**：
- **连接状态（L/R/X tag）**：`extern active_transport` → `active_transport->api->get_available_source_ids()` 返回已连接 slot 数组，权威且不经过任何事件队列
- **电量（百分比/竖条）**：`zmk_split_central_get_peripheral_battery_level(i, &lvl)` 读 central 缓存，只驱动 bar/文本
- **已连接但 cache=0**（READ 失败场景）→ 显示 L/R + `--`（区分"已连接电量未知"与"未连接"）
- **电量动画**：800ms lv_anim（上升 overshoot、下降 ease-out），百分比文本与 bar 同一 exec 回调同步；tag 瞬时切换与动画分离
- 连接变化 → `brightness_wake_screen_on_reconnect()` 唤屏（prev_connected 跟踪）
- slot = 连接顺序，"L"/"R" 是序号非物理边
- **历史教训**：电池事件管道（READ 失败未检查、事件队列静默丢弃、ADC 未就绪返回 0）导致 dongle 重启后连上了也显示 X——不再用电池判断连接

## showkey_status 特殊机制
- **跨半区 shift 必须读 HID report**：`zmk_hid_get_keyboard_report()->body.modifiers`（central 汇总所有半区，与宿主收到的字节一致）。keycode 事件自带的 implicit/explicit_modifiers 只描述该键绑定内嵌的 mod（如 LS(SEMI)），看不到另一半按的 Shift，不可用
- **Caps Lock 大小写**：字母（usage 0x04-0x1D）大写 ⇔ Shift XOR Caps Lock；Caps Lock 读 `zmk_hid_indicators_get_current_profile() & HID_INDICATOR_CAPS_LOCK`（主机 USB LED report），需 `CONFIG_ZMK_HID_INDICATORS=y`；数字/标点只受 Shift
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
