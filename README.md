# X仪表

这是 X仪表 的 ESP32 固件和微信小程序项目。V3.0 是面向客户体验的正式 UI 版本：仪表端使用 170x320 ST7789 彩屏横屏显示，界面按 320x170 固定坐标设计；小程序用于连接 X仪表、搜索附近电池、选择电池、管理历史电池和查看基础状态。

## 当前正式版本

- 固件目录：`esp32/ANT_BMS_TFT_Dashboard_V3_0_CustomerUX/`
- 小程序目录：`miniprogram/`
- 固件版本：`ANT_BMS_TFT_Dashboard_V3_0_CustomerUX`
- 屏幕驱动：`TFT_eSPI`
- 屏幕方向：横屏 `320x170`

旧的 V2.2、V2.4、V2.4.1、V2.4.3 和 V2.5 目录保留用于追溯，不作为当前客户版本首选。

## 仪表端体验

- 主界面按参考 UI 重新绘制：顶部温度和连接图标，左侧电压/电流，中间 SOC 圆环，右侧总容量/剩余容量。
- 中文显示改为内置 UTF-8 点阵字库，覆盖仪表主界面和状态页所有客户可见中文。
- 状态页取消小猫和二轮车动画，改为抽象 loading 圆环、雷达扫描和手机呼吸点。
- 自动连接流程保持原目标：先尝试上次保存的电池，失败后扫描附近信号最强的电池，失败后持续重试。
- 主界面不显示 MOS、状态正常、RSSI、MAC、UUID、BLE 名称、Scan window 或工程状态。
- 屏幕绘制使用 Sprite 缓冲和局部刷新，减少闪烁并降低不必要的 SPI 刷新。

## 小程序体验

- 用户界面统一称为“X仪表”和“电池”。
- 电池列表显示真实蓝牙名称，不再显示“电池 1 / 电池 2”这类占位名。
- 电池页用于搜索附近电池、选择电池、连接此电池。
- 电池页支持删除单个历史电池和清空历史电池，并同步到仪表端。
- 设置页支持重启 X仪表。
- 客户页面不显示 MAC、RSSI、dBm、ESP32、BLE、UUID 等工程信息。
- 扫描、选择、保存、连接的底层协议和接口保持原有逻辑。

## 固件编译

在 Arduino CLI 中使用 ESP32 `huge_app` 分区编译：

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app esp32/ANT_BMS_TFT_Dashboard_V3_0_CustomerUX
```

本机已使用 ESP32 core 2.0.11、TFT_eSPI 2.5.43、NimBLE-Arduino 2.5.0 编译通过。
