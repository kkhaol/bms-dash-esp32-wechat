#ifndef JK_BLE_H
#define JK_BLE_H
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "BmsData.h"
#include "JkProtocol.h"

#define JK_SCAN_RESULT_MAX 8

struct JkScanItem {
  String address;
  String name;
  int rssi;
};

class JkBle {
public:
  JkBle();
  bool begin();
  bool scanAndConnect(uint32_t seconds=5);
  bool connectByAddress(const String& address);
  uint8_t scanDevices(uint32_t seconds=5);
  bool connectDeviceByIndex(uint8_t index);
  bool connected() const;
  void loop();

  uint8_t getScanCount() const { return scanCount_; }
  const JkScanItem& getScanItem(uint8_t i) const { return scanItems_[i]; }
  const String& getConfiguredAddress() const { return configuredAddress_; }
  void setConfiguredAddress(const String& mac);
  uint8_t getScanAttempt() const { return scanAttempt_; }
  void setProtocol32S(bool enable){ protocol32S_=enable; protocol_.setProtocol32S(enable); }
  bool isProtocol32S() const { return protocol32S_; }

private:
  NimBLEClient* client_;
  NimBLERemoteCharacteristic* ch_;
  JkProtocol protocol_;
  uint8_t counter_;
  uint32_t lastRequest_;\n  uint32_t lastReconnectAttempt_;
  uint8_t scanCount_;
  uint8_t scanAttempt_;
  bool protocol32S_;
  String configuredAddress_;
  JkScanItem scanItems_[JK_SCAN_RESULT_MAX];

  static JkBle* instance_;
  static void notifyCallback(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool);
  void handleNotification(const uint8_t*, size_t);
  void request(uint8_t);
  bool isCandidate(const NimBLEAdvertisedDevice*) const;
  void setStatus(BmsBootState state, const String& message);
};
#endif
