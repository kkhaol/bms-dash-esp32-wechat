#include <Arduino.h>
#include <WiFi.h>
#include "../tft_setup.h"
#include "BmsData.h"
#include "JkBle.h"
#include "Display.h"
#include "WebConfig.h"

static JkBle jk;
static Display display;
static WebConfig webConfig;

static const char* AP_SSID="JK-BMS-SETUP";
static const char* AP_PASSWORD="12345678";

static void startHotspot(){
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID,AP_PASSWORD);
  IPAddress ip=WiFi.softAPIP();

  g_bmsData.hotspot=true;
  g_bmsData.hotspotIp=ip.toString();
  g_bmsData.bootState=BOOT_HOTSPOT;
  g_bmsData.statusMessage="等待网页设置";
  webConfig.begin(&jk);

  Serial.print("热点SSID: ");
  Serial.println(AP_SSID);
  Serial.print("热点IP: ");
  Serial.println(ip);
}

void setup(){
  Serial.begin(115200);
  delay(500);

  Serial.println("ESP32-C3 JK BMS + ST7789 V2");
  Serial.println("BL=5 CS=3 DC=2 RES=10 SDA=7 SCL=6");

  display.begin();
  jk.begin();

  // 开机最多自动扫描三次；三次都失败后进入AP热点设置模式
  bool ok=false;
  for(uint8_t i=0;i<3 && !ok;i++){
    ok=jk.scanAndConnect(5);
    if(!ok) delay(300);
  }

  if(!ok) startHotspot();
}

void loop(){
  jk.loop();
  webConfig.loop();

  static uint32_t drawMs=0;
  if(millis()-drawMs>=200){
    drawMs=millis();
    display.update(g_bmsData);
  }

  delay(5);
}
