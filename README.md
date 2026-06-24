# BMS-DASH ESP32 微信小程序配置工具

BMS-DASH 是一个用于电动车小仪表的配置项目。

ESP32 小仪表负责连接蚂蚁 BMS，并在屏幕上显示电池数据；微信小程序只负责配置 ESP32，比如搜索仪表、选择要连接的 BMS、清除保存配置、重启 ESP32。手机不会长期读取 BMS，也不会直接连接 BMS。

## 项目内容

- `miniprogram/`：微信原生小程序代码。
- `esp32/ANT_BMS_TFT_Dashboard_V2_2_StateMachine/`：ESP32 Arduino 工程。
- `esp32/ANT_BMS_TFT_Dashboard_V2_2test/`：临时测试版 ESP32 工程，开机会自动连接附近信号最强的疑似蚂蚁 BMS。

## 小程序能做什么

- 搜索名称以 `BMS-DASH` 开头的 ESP32 仪表。
- 通过 BLE 连接 ESP32。
- 查看 ESP32 与 BMS 的连接状态。
- 显示电压、电流、SOC。
- 让 ESP32 扫描附近 BMS。
- 选择指定 BMS，让 ESP32 连接并保存。
- 从历史连接过的 BMS 里快速选择。
- 清除 ESP32 保存的 BMS。
- 重启 ESP32。

## ESP32 开机逻辑

ESP32 开机后只会尝试连接上次成功保存的 BMS。

如果没有保存过 BMS，或者找不到上次保存的 BMS，ESP32 会等待小程序配置。它不会自动连接信号最强的 BMS。

选择新的 BMS 时，只有 ESP32 连接成功后才会保存为下次开机自动连接目标；如果连接失败，原来的保存配置不会被覆盖。

小仪表屏幕也会显示当前状态，例如“启动中”“配置蓝牙已开启”“连接上次 BMS”“等待手机配置”“扫描 BMS 中”“切换 BMS 中”“BMS 连接失败”。连接失败时屏幕会提示原保存配置不变，然后回到等待手机配置。

## 临时测试版 v2_2test

如果微信小程序还没上线，可以先烧录：

```text
esp32/ANT_BMS_TFT_Dashboard_V2_2test/
```

这个测试版会在开机后直接扫描附近疑似蚂蚁 BMS，并连接 RSSI 最强的一台。连接成功后会保存到 ESP32 的 NVS。

测试版屏幕会显示“连接最强 BMS”，串口日志里会出现 `[POLICY-TEST]`。如果附近有多台 BMS，这个版本可能连到信号最强但不是你想要的那台，所以只建议临时测试使用。

## 小程序使用方法

1. 用微信开发者工具导入 `miniprogram/` 文件夹。
2. 导入时使用自己的小程序 AppID；只本地测试也可以先用测试号。
3. 用真机调试，小程序模拟器通常不能完整测试 BLE。
4. 打开手机蓝牙，并允许小程序使用蓝牙权限。
5. 在“仪表”页点击“搜索仪表”。
6. 找到 `BMS-DASH` 开头的设备后点击连接。
7. 到“BMS”页点击“扫描 BMS”。
8. 在扫描结果里选择你的 BMS，等待提示保存成功。
9. 以后 ESP32 开机后会自动尝试连接这个已保存的 BMS。

## ESP32 使用方法

1. 正式版用 Arduino IDE 或 Arduino CLI 打开 `esp32/ANT_BMS_TFT_Dashboard_V2_2_StateMachine/`；临时测试版打开 `esp32/ANT_BMS_TFT_Dashboard_V2_2test/`。
2. 选择 ESP32 开发板。
3. 分区建议选择 `Huge APP`。
4. 安装工程需要的库：Adafruit GFX、Adafruit ST7735/ST7789、U8g2、U8g2_for_Adafruit_GFX、NimBLE-Arduino。
5. 编译并烧录到 ESP32。
6. 打开串口监视器，波特率 `115200`。

串口里会打印状态变化，例如：

```text
[STATE] BOOTING -> BLE_SETUP_READY
[STATE] BLE_SETUP_READY -> TRY_CONNECT_LAST_BMS
[STATE] TRY_CONNECT_LAST_BMS -> BMS_CONNECTED
```

这些日志可以帮助确认 ESP32 是否按预期连接保存过的 BMS。
