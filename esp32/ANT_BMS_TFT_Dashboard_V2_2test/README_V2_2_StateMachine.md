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

## Screen State UI

The 1.9 inch TFT has a matching UI for each setup state:

- `BOOTING`: startup and initialization.
- `BLE_SETUP_READY`: WeChat configuration BLE service is ready.
- `TRY_CONNECT_LAST_BMS`: scanning only the saved BMS MAC.
- `WAIT_CONFIG`: waiting for the mini program to choose or reconnect a BMS.
- `SCANNING_BMS`: the mini program requested a 5-second BMS scan.
- `SWITCHING_BMS`: switching to a newly selected BMS; NVS is updated only after success.
- `BMS_CONNECT_FAILED`: connection failed and the old saved config is unchanged.
- `BMS_CONNECTED`: normal dashboard data view.

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

- Sketch: 1,236,281 bytes, 39% of 3,145,728 bytes
- RAM: 35,640 bytes, 10% of 327,680 bytes
