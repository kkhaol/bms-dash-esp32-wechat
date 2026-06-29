# ANT-BMS 小仪表 V2.4.1 显示配置修复版

这是 V2.4 客户 UI 的显示配置修复版。

## 修复内容

- 在 `TFT_eSPI.h` 之前显式加载本项目的 `tft_setup.h`。
- 确保 ST7789、320x170 横屏、GPIO18/23/17/16/5 等屏幕参数真正参与编译。
- 不改变 BMS 自动连接、小程序扫描选择、数据解析和存储逻辑。

## 屏幕参数

- 驱动库：TFT_eSPI
- 屏幕：ST7789
- 物理分辨率：170 x 320
- UI 方向：横屏 320 x 170
- 接线：SCL GPIO18，SDA GPIO23，RES GPIO17，DC GPIO16，CS GPIO5，BLK 接 3V3

## 编译

```powershell
& 'D:\tool\Arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --build-path 'D:\tool\Arduino\build\ANT_BMS_TFT_Dashboard_V2_4_1_DisplayFix' 'D:\tool\Arduino\Projects\ANT_BMS_TFT_Dashboard_V2_4_1_DisplayFix'
```
