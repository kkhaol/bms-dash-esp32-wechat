# ANT BMS TFT Dashboard V2.2 State Machine

This version keeps the WeChat mini program as a configuration tool only.
The phone connects to the ESP32 `BMS-DASH-SETUP` BLE service and never connects
directly to the ANT BMS.

## Boot Policy

State flow:

1. `BOOTING`
2. `BLE_SETUP_READY`
3. If `sel_bms_mac` exists in NVS: `TRY_CONNECT_LAST_BMS`
4. If that exact MAC is found and connected: `BMS_CONNECTED`
5. If no saved MAC exists, it is not found, or connection fails: `WAIT_CONFIG`

Important rules:

- Boot never connects to the strongest BMS.
- Boot only tries the last successfully saved BMS MAC.
- `SELECT_BMS:<mac>` saves to NVS only after the selected BMS connects.
- A failed switch returns `{"type":"error","message":"connect_failed"}` and keeps the previous saved config.
- Every state change is logged as `[STATE] old -> new: reason`.

## Mini Program Commands

- `PING`
- `GET_STATUS`
- `SCAN_BMS`
- `SELECT_BMS:<mac>`
- `FORGET_BMS`
- `RECONNECT_BMS`
- `REBOOT`

`SET_AUTO_STRONGEST` is intentionally not supported in this version.

## Build Check

Compiled with:

```powershell
D:\tool\Arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" --libraries "C:\Users\chinalion\Documents\Arduino\libraries" .\Projects\ANT_BMS_TFT_Dashboard_V2_2_StateMachine
```

Result:

- Sketch: 1,234,685 bytes, 39% of 3,145,728 bytes
- RAM: 35,640 bytes, 10% of 327,680 bytes
