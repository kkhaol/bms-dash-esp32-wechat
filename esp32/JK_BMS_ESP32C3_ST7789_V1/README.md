# ESP32-C3 + JK BMS BLE + ST7789 1.9"

## 屏幕接线

- BL = GPIO5
- CS = GPIO3
- DC = GPIO2
- RES = GPIO10
- SDA / MOSI = GPIO7
- SCL / SCK = GPIO6

## 开机连接逻辑

1. 屏幕显示 **CONNECT BATTERY**。
2. 自动扫描 JK / JIKONG / BMS 蓝牙设备。
3. 最多自动扫描 **3 次**，每次 5 秒，并显示 `1/3、2/3、3/3` 进度。
4. 找到设备后自动连接信号最强的候选设备。
5. 连接成功后显示实时 BMS 数据。
6. 三次都失败后自动进入 Wi-Fi AP 热点模式。

## 热点设置

热点参数：

- SSID：`JK-BMS-SETUP`
- 密码：`12345678`
- 默认网页：`http://192.168.4.1`

手机连接热点后，浏览器打开 `192.168.4.1`，可以：

- 扫描蓝牙 JK BMS；
- 查看设备名称、MAC、RSSI；
- 选择指定电池连接；
- 设置 JK02_24S / JK02_32S；
- 保存 MAC，下一次开机优先连接该 MAC；
- 查看电压、电流、SOC 和蓝牙连接状态。

## 工程结构

    JK_BMS_ESP32C3_ST7789_V1/
    ├── platformio.ini
    ├── tft_setup.h
    ├── JkConfig.h
    ├── README.md
    └── src/
        ├── main.cpp
        ├── BmsData.h/.cpp
        ├── JkProtocol.h/.cpp
        ├── JkBle.h/.cpp
        ├── Display.h/.cpp
        └── WebConfig.h/.cpp

## BLE

使用 NimBLE-Arduino。当前 JK BLE 连接使用常见的：

- Service：`FFE0`
- Characteristic：`FFE1`

连接后发送 JK 查询命令，并接收 `0x02` 主数据帧。

## 注意

当前网页配置是本地热点配置页，不需要互联网。Wi-Fi AP 与 BLE 可以同时工作；网页连接成功后，屏幕继续显示 BMS 实时数据，并在状态栏标记 `AP`。
