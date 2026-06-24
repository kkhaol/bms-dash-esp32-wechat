# ANT BMS TFT Dashboard v2_2test

这是 V2.2 的临时测试版，用于微信小程序还没上线时让小仪表可以独立上车测试。

## 和正式 V2.2 的区别

正式 V2.2 的开机策略是：只连接上次成功保存的 BMS，找不到就等待小程序配置。

`v2_2test` 的开机策略是：启动后直接扫描附近疑似蚂蚁 BMS，选择 RSSI 最强的设备连接。

连接成功后，测试版会把这个 BMS 保存到 NVS，方便状态显示和后续 `RECONNECT_BMS` 命令使用。连接失败时不会覆盖原有保存配置，会回到等待手机配置。

## 开机状态

测试版新增状态：

- `TRY_CONNECT_STRONGEST_BMS`

屏幕会显示“连接最强 BMS / v2_2test 临时逻辑 / 扫描后自动连接”。

串口日志会打印：

```text
[POLICY-TEST] v2_2test boot auto-connects the strongest ANT/BMS candidate
[STATE] BLE_SETUP_READY -> TRY_CONNECT_STRONGEST_BMS: v2_2test boot scans strongest BMS
[POLICY-TEST] Boot selected strongest BMS: name="..." mac=... rssi=...
```

## 注意

这个版本会自动连接附近信号最强的疑似 ANT/BMS 设备。如果附近有多台 BMS，不适合作为长期正式逻辑使用。

正式版本仍然使用：

```text
ANT_BMS_TFT_Dashboard_V2_2_StateMachine
```

测试版本使用：

```text
ANT_BMS_TFT_Dashboard_V2_2test
```
