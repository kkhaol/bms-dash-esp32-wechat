# X仪表

这是 X仪表 的 ESP32 固件和微信小程序项目。V2.4 是第一版面向客户体验的正式 UI：仪表端使用 170x320 ST7789 彩屏横屏显示，界面按 320x170 设计；小程序用于连接 X仪表、搜索附近电池、选择电池并查看基础状态。

## 当前正式版本

- 固件目录：`esp32/ANT_BMS_TFT_Dashboard_V2_4_1_DisplayFix/`
- 小程序目录：`miniprogram/`
- 固件版本：`ANT_BMS_TFT_Dashboard_V2_4_1_DisplayFix`
- 屏幕驱动：TFT_eSPI
- 屏幕方向：横屏 320x170

旧的 V2.2 和 V2.4 目录仍保留用于追溯，不作为当前客户版首选。

## 仪表端体验

- 开机约 2 秒摩托车元素动画。
- 自动连接逻辑保持为：先尝试上次保存的电池，失败后再尝试附近信号更强的电池。
- 连接过程使用中文状态，例如“正在连接电池”“正在搜索电池”。
- 主界面只突出 SOC、电压、温度、压差、剩余容量。
- 不再把功率作为主视觉大数字显示。
- 顶部小图标显示电池连接和手机连接状态；未连接时不显示多余警告图标。
- 屏幕绘制迁移到 TFT_eSPI，并使用 Sprite 缓冲减少闪烁。

## 小程序体验

- 统一称为“X仪表”和“电池”。
- 首页显示仪表连接、电池连接和核心数据。
- 电池页用于搜索附近电池、选择电池、连接此电池。
- 客户页面不显示 MAC、RSSI、dBm、ESP32、BLE、设备名等工程信息。
- 扫描、选择、保存、连接的底层逻辑保持原有协议和接口。

## 固件编译

在 Arduino CLI 中使用 ESP32 `huge_app` 分区编译：

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app esp32/ANT_BMS_TFT_Dashboard_V2_4_1_DisplayFix
```

本机已使用 ESP32 core 2.0.11、TFT_eSPI 2.5.43、NimBLE-Arduino 2.5.0 编译通过。
