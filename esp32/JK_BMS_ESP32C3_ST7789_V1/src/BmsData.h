#ifndef BMS_DATA_H
#define BMS_DATA_H
#include <Arduino.h>
#define JK_MAX_CELLS 32

enum BmsBootState {
  BOOT_START = 0,
  BOOT_SCANNING = 1,
  BOOT_CONNECTING = 2,
  BOOT_CONNECTED = 3,
  BOOT_HOTSPOT = 4
};

struct BmsData {
  bool online=false, valid=false;
  uint8_t cellCount=0;
  float totalVoltage=0, current=0, power=0, soc=0;
  float minCellVoltage=0, maxCellVoltage=0, deltaCellVoltage=0;
  uint8_t minCell=0, maxCell=0;
  float cellVoltage[JK_MAX_CELLS]={0};
  float temperature1=0, temperature2=0, mosTemperature=0;
  float remainingCapacityAh=0, totalCapacityAh=0, balancingCurrent=0;
  // 剩余里程 = 剩余容量(Ah) × 电压(V) ÷ 每公里耗电(Wh/km)
  float remainingRangeKm=0;
  float energyConsumptionWhKm=100.0f;
  bool charging=false, discharging=false, balancing=false, heating=false;
  uint32_t errors=0, updateMs=0;

  String mac, deviceName, softwareVersion, hardwareVersion;

  BmsBootState bootState=BOOT_START;
  uint8_t scanAttempt=0;
  uint8_t scanMax=3;
  bool hotspot=false;
  String hotspotIp;
  String statusMessage="正在启动";
};

extern BmsData g_bmsData;
#endif
