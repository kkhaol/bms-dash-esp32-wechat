# ANT-BMS TFT 仪表 V2.1 微信 BLE 配置版

本固件基于 V2.0 仪表版本，保留原来的屏幕显示和蚂蚁 BMS 数据解析逻辑，新增给微信小程序使用的 BLE 配置服务。

## 项目文件

主程序：

```text
D:\tool\Arduino\Projects\ANT_BMS_TFT_Dashboard_V2_1_BLE_Setup\ANT_BMS_TFT_Dashboard_V2_1_BLE_Setup.ino
```

## BLE 角色

ESP32 同时承担两个 BLE 角色：

- 对手机：作为 BLE Peripheral，广播名称为 `BMS-DASH-SETUP`。
- 对 BMS：作为 BLE Central，扫描并连接附近疑似蚂蚁 BMS，使用服务 `0xFFE0` 和特征 `0xFFE1` 读取数据。

## 微信小程序配置服务

```text
Service UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX UUID:      6E400002-B5A3-F393-E0A9-E50E24DCCA9E
TX UUID:      6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

小程序向 RX 写入命令，ESP32 通过 TX notify 返回 JSON。

支持命令：

```text
PING
GET_STATUS
SCAN_BMS
SELECT_BMS:<mac>
SET_AUTO_STRONGEST:1
SET_AUTO_STRONGEST:0
FORGET_BMS
RECONNECT_BMS
REBOOT
```

## 返回格式

ESP32 每次返回一条 JSON 文本，结尾固定带 `\n`。

如果 JSON 长度超过当前 BLE MTU，固件会自动分包 notify。小程序端需要把收到的数据拼接起来，直到遇到 `\n` 再当作一条完整 JSON 解析。

常见返回：

```json
{"type":"pong"}
{"type":"status","esp":"BMS-DASH","bms_connected":true,"selected_bms":"A4:C1:38:12:34:56","name":"ANT-BMS","voltage":72.4,"current":-3.2,"soc":86}
{"type":"scan_start"}
{"type":"bms_found","name":"ANT-BMS","mac":"A4:C1:38:12:34:56","rssi":-48}
{"type":"scan_done"}
{"type":"select_ok","mac":"A4:C1:38:12:34:56"}
{"type":"error","message":"xxx"}
```

## 保存配置

使用 ESP32 Preferences/NVS 保存配置，命名空间为 `bms-dash`。

保存内容：

- 已选择的 BMS MAC
- 已选择的 BMS 名称
- 是否允许自动连接信号最强的疑似 BMS

注意：ESP32 NVS key 最长 15 个字符，所以代码内部使用了缩短后的 key，但含义对应 `selected_bms_mac`、`selected_bms_name`、`auto_strongest`。

## 启动连接逻辑

1. 开机后读取已保存的 BMS MAC。
2. 如果有保存 MAC，先扫描附近设备并尝试连接同 MAC 的 BMS。
3. 如果找不到保存的 BMS，且 `auto_strongest=true`，连接扫描到的信号最强疑似 BMS。
4. 如果没有保存 MAC，且 `auto_strongest=false`，等待微信小程序选择。
5. `SCAN_BMS` 会触发 10 秒扫描，并逐条 notify 返回扫描到的 BMS。
6. `SELECT_BMS:<mac>` 会保存该 MAC，并立即尝试连接。
7. `FORGET_BMS` 会清除保存的 BMS 配置并断开当前 BMS。

## Arduino 设置

开发板：

```text
ESP32 Dev Module
```

分区必须选择：

```text
Huge APP
```

FQBN：

```text
esp32:esp32:esp32:PartitionScheme=huge_app
```

串口波特率：

```text
115200
```

## 依赖库

需要安装：

```text
Adafruit ST7735 and ST7789 Library
Adafruit GFX Library
Adafruit BusIO
U8g2
U8g2_for_Adafruit_GFX
NimBLE-Arduino
```

当前本机已安装并验证使用 `NimBLE-Arduino 2.5.0` 编译通过。

## 编译命令

```powershell
& 'D:\tool\Arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --build-path 'D:\tool\Arduino\build\ANT_BMS_TFT_Dashboard_V2_1_BLE_Setup' 'D:\tool\Arduino\Projects\ANT_BMS_TFT_Dashboard_V2_1_BLE_Setup'
```

## 烧录命令

先确认 ESP32 当前串口，例如 `COM5` 或 `COM6`，再把下面命令里的 `COMx` 换成实际串口：

```powershell
& 'D:\tool\Arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' upload -p COMx --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --build-path 'D:\tool\Arduino\build\ANT_BMS_TFT_Dashboard_V2_1_BLE_Setup' 'D:\tool\Arduino\Projects\ANT_BMS_TFT_Dashboard_V2_1_BLE_Setup'
```
