#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include "esp_system.h"
#include "tft_setup.h"
#include "BmsData.h"
#include "JkBle.h"
#include "Display.h"
#include "WebConfig.h"

static JkBle jk;
static Display display;
static WebConfig webConfig;
static const char* AP_SSID="JK-BMS-SETUP";
static const char* AP_PASSWORD="12345678";

RTC_DATA_ATTR static uint8_t rtcFailedScanAttempts=0;

static void startHotspot(){
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID,AP_PASSWORD);
  IPAddress ip=WiFi.softAPIP();
  g_bmsData.hotspot=true;
  g_bmsData.hotspotIp=ip.toString();
  g_bmsData.bootState=BOOT_HOTSPOT;
  g_bmsData.statusMessage="等待网页设置";
  webConfig.begin(&jk);
  Serial.printf("HOTSPOT: %s %s heap=%u\\n",AP_SSID,ip.toString().c_str(),ESP.getFreeHeap());
}

void setup(){
  Serial.begin(115200);
  delay(50);

  esp_reset_reason_t resetReason=esp_reset_reason();
  Serial.println();
  Serial.printf("ESP32 reset reason: %d\\n",(int)resetReason);

  pinMode(TFT_BL,OUTPUT);
  digitalWrite(TFT_BL,LOW);
  delay(20);

  g_bmsData.scanMax=3;
  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.hotspot=false;

  // 初始化显示和 BLE。
  display.begin();
  jk.begin();

  // ================================
  // 开机优先检查“已经保存的蓝牙地址”
  // ================================
  const String savedMac=jk.getConfiguredAddress();

  if(savedMac.length()==0){
    // 没有保存过蓝牙，直接进入热点配网。
    g_bmsData.scanAttempt=0;
    g_bmsData.bootState=BOOT_HOTSPOT;
    g_bmsData.statusMessage="未保存蓝牙，进入配网";
    display.update(g_bmsData);
    delay(100);
    startHotspot();
    display.update(g_bmsData);
    return;
  }

  // 有保存地址：只尝试连接这个地址，不再盲目扫描其它设备。
  g_bmsData.scanAttempt=1;
  g_bmsData.bootState=BOOT_CONNECTING;
  g_bmsData.statusMessage="连接已保存蓝牙";
  g_bmsData.mac=savedMac;
  display.update(g_bmsData);
  delay(100);

  Serial.printf("BOOT: saved JK MAC = %s\\n",savedMac.c_str());

  bool connectedOk=jk.connectByAddress(savedMac);

  // GATT 连接建立后，还必须收到有效 JK 数据，才算真正成功。
  if(connectedOk && jk.connected()){
    uint32_t verifyStart=millis();
    while(jk.connected() && !g_bmsData.valid &&
          millis()-verifyStart<4000UL){
      jk.loop();
      delay(20);
    }
    connectedOk=jk.connected() && g_bmsData.valid;
  }

  if(connectedOk){
    g_bmsData.bootState=BOOT_CONNECTED;
    g_bmsData.online=true;
    g_bmsData.statusMessage="已连接保存的JK电池";
    display.update(g_bmsData);
    Serial.println("BOOT: saved Bluetooth connected and JK data valid.");
    return;
  }

  // 保存地址连接失败：进入热点，让网页重新扫描/选择。
  g_bmsData.online=false;
  g_bmsData.valid=false;
  g_bmsData.bootState=BOOT_HOTSPOT;
  g_bmsData.statusMessage="蓝牙连接失败，进入配网";
  display.update(g_bmsData);
  delay(100);

  Serial.println("BOOT: saved Bluetooth connection failed, entering hotspot.");
  startHotspot();
  display.update(g_bmsData);
}
void loop(){
  jk.loop();
  webConfig.loop();
  static uint32_t drawMs=0;
  if(millis()-drawMs>=500){
    drawMs=millis();
    display.update(g_bmsData);
  }
  delay(5);
}
