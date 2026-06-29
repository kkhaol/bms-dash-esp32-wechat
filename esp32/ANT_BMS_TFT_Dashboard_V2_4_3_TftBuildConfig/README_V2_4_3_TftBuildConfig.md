# ANT-BMS 小仪表 V2.4.3 TFT_eSPI 配置修复版

这是针对“背光亮但无显示内容”的 TFT_eSPI 配置修复版。

## 修复内容

- 保留 V2.4.2 的 `BOOT_OK`、`displayTest()` 和 Sprite 创建日志。
- 新增 `build_opt.h`，通过编译参数把本项目 `tft_setup.h` 强制传给 TFT_eSPI 库本体。
- 修复 TFT_eSPI 库源码仍使用默认 `User_Setup.h` 的问题。
- 目标是让库真正使用 ST7789、170x320、GPIO18/23/5/16/17，而不是默认 ILI9341 配置。

## 不改变

- 不改变 BMS 自动连接逻辑。
- 不改变小程序扫描、选择、保存逻辑。
- 不改变旧代码确认过的屏幕参数：rotation = 1，invert = true，SPI 40MHz。
