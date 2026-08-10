# AGENTS.md — drivers/display（掉包 ZMK 内置 ST7789V）

**定制 ST7789V 驱动目录**：用同名 .c 替换 Zephyr 内置驱动，新增 MADCTL 旋转支持（横屏/翻转）。不 patch 上游、直接掉包。父目录 AGENTS.md 已覆盖 widget/构建中枢，这里只讲掉包机制与面板方向。

## 掉包机制（本目录核心）

- 根 CMakeLists 仅当 `CONFIG_SHIELD_DONGLE_SCREEN` 时 `add_subdirectory(drivers/display)`
- CMakeLists.txt 三行完成替换：
  1. `zephyr_library_amend()` — 复用 Zephyr 内置驱动所属的 library，不新建
  2. `set_source_files_properties(${ZEPHYR_BASE}/drivers/display/display_st7789v.c ... HEADER_FILE_ONLY ON)` — 把上游 .c 标记为"仅头文件"、不进编译（TARGET_DIRECTORY ${lib_name} 指定目标）
  3. `zephyr_library_sources(display_st7789v.c)` — 编译本目录同名 .c 顶替
- 同名文件 + 同 `DT_DRV_COMPAT sitronix_st7789v` = 掉包透明，dts/overlay 无需感知
- 本目录 .c 基本照抄上游（struct、ST7789V_INIT 宏、初始化序列一致），差异集中在方向支持

## 相对上游新增：方向支持

- `st7789v_data` 新增 `orientation` 字段，初始 `DISPLAY_ORIENTATION_NORMAL`
- 新增 `st7789v_set_orientation()`：只改写 MADCTL 的 MY/MX/MV 位，保留 mdac 中的 ML|BGR|MH 位；四方向映射 NORMAL→MV_NORMAL / 90→MY_BOTTOM_TO_TOP+MV_REVERSE / 180→MY+MX / 270→MX+MV_REVERSE；写回 `x/y_offset`（90/270 交换 row/col）
- capabilities 查询：90/270 时交换 x/y 分辨率（MV 位换轴），报告 `current_orientation`
- API 表新增 `.set_orientation = st7789v_set_orientation`（上游该 API 为空，故需掉包）

## 与 screen_rotate_init.c 交互

- `SYS_INIT(disp_set_orientation, APPLICATION, 60)` 在应用初始化期调 `display_set_orientation`
- HORIZONTAL × FLIPPED 四组合 → ROTATED_90 / 270 / NORMAL / 180（该文件用 `#ifdef`，与 custom_status_screen.c 的 `#if` 不一致，见父文档）
- 用 `DEVICE_DT_GET(DT_CHOSEN(zephyr_display))`，`device_is_ready` 失败返回 -EIO

## 屏幕硬件参数

- 面板 240x280、y-offset=20 → 可视区 240x240；x-offset=0（两块 overlay 相同）
- RGB565（CONFIG_ST7789V_RGB565 → ST7789V_PIXEL_SIZE=2；`#ifdef/#elif/#else` 三分支决定 RGB565/BGR565/RGB888）
- 接线/SPI 配置不在本目录，见 `boards/shields/dongle_screen/boards/` overlay

## 修改注意

- 本驱动改动影响**所有** board overlay（nice_nano + xiao_ble 共用面板参数）
- 改屏幕尺寸/offset：须同步两处 overlay 的 width/height/x-offset/y-offset，否则旋转时 offset 交换错位
- 面板参数来自 dts（`DT_INST_PROP` 读 width/height/x_offset/y_offset），非编译期常量，overlay 改了驱动自动跟随
