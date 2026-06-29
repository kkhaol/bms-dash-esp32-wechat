# ANT-BMS 小仪表 V2.4 客户 UI 正式版

这是面向客户体验的第一版小仪表 UI。固件保留原有 NimBLE 连接、BMS 数据解析、小程序扫描/选择/保存流程，主要重做屏幕显示体验。

## 屏幕

- 驱动库：TFT_eSPI
- 屏幕：ST7789
- 物理分辨率：170 x 320
- UI 方向：横屏 320 x 170
- 接线：SCL GPIO18，SDA GPIO23，RES GPIO17，DC GPIO16，CS GPIO5，BLK 接 3V3

TFT_eSPI 的本项目配置在 `tft_setup.h`，不需要修改全局库配置。

## 开机连接策略

1. 开机播放约 2 秒摩托车元素动画。
2. 优先扫描并连接上一次保存的 BMS。
3. 如果上次保存的 BMS 不可用，则自动连接附近信号最强的 BMS。
4. 小程序仍可扫描附近 BMS、选择 BMS、保存选择结果。

## UI 信息

主界面只保留客户需要的核心数据：

- SOC
- 电压
- 温度
- 压差
- 剩余容量

当前不再把功率作为主视觉显示。BMS 和手机连接状态使用顶部小图标表示，未连接时不显示多余警告图标。

## 编译

```powershell
& 'D:\tool\Arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --build-path 'D:\tool\Arduino\build\ANT_BMS_TFT_Dashboard_V2_4_CustomerUI' 'D:\tool\Arduino\Projects\ANT_BMS_TFT_Dashboard_V2_4_CustomerUI'
```

已在本机使用 ESP32 core 2.0.11、TFT_eSPI 2.5.43、NimBLE-Arduino 2.5.0 编译通过。
