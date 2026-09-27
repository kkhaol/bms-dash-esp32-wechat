#include "Display.h"
#include "FontGB2312.h"

static uint16_t lerp565(uint16_t a, uint16_t b, uint16_t t) {
  uint8_t ar=(a>>11)&0x1F, ag=(a>>5)&0x3F, ab=a&0x1F;
  uint8_t br=(b>>11)&0x1F, bg=(b>>5)&0x3F, bb=b&0x1F;
  uint8_t rr=ar+((br-ar)*t)/100;
  uint8_t rg=ag+((bg-ag)*t)/100;
  uint8_t rb=ab+((bb-ab)*t)/100;
  return (rr<<11)|(rg<<5)|rb;
}

bool Display::changed(float a, float b, float eps) const {
  return fabsf(a-b) >= eps;
}

void Display::begin() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft_.init();
  tft_.setRotation(1);
  tft_.fillScreen(TFT_BLACK);

  statusSprite_.setColorDepth(16);
  socSprite_.setColorDepth(16);
  leftInfoSprite_.setColorDepth(16);
  rowSprite_.setColorDepth(16);
  barSprite_.setColorDepth(16);

  statusSprite_.createSprite(320, 22);
  socSprite_.createSprite(140, 65);
  leftInfoSprite_.createSprite(140, 30);
  rowSprite_.createSprite(175, 28);
  barSprite_.createSprite(304, 8);

  statusSprite_.fillSprite(TFT_BLACK);
  socSprite_.fillSprite(TFT_BLACK);
  leftInfoSprite_.fillSprite(TFT_BLACK);
  rowSprite_.fillSprite(TFT_BLACK);
  barSprite_.fillSprite(TFT_BLACK);

  initialized_ = true;
  firstDashboard_ = true;
  drawFullPage(g_bmsData);
}

void Display::update(const BmsData& d) {
  if (!initialized_) return;

  if (d.bootState != lastBootState_ || (d.hotspot != lastData_.hotspot)) {
    drawFullPage(d);
    lastBootState_ = d.bootState;
    lastData_ = d;
    lastOnline_ = d.online;
    lastDeviceName_ = d.deviceName;
    return;
  }

  if (d.bootState != BOOT_CONNECTED && !d.online) {
    if (d.statusMessage != lastData_.statusMessage ||
        d.scanAttempt != lastData_.scanAttempt ||
        d.hotspotIp != lastData_.hotspotIp ||
        d.mac != lastData_.mac) {
      drawFullPage(d);
      lastData_ = d;
    }
    return;
  }

  drawDashboard(d, firstDashboard_);
  firstDashboard_ = false;
  lastData_ = d;
  lastOnline_ = d.online;
  lastDeviceName_ = d.deviceName;
}

void Display::drawFullPage(const BmsData& d) {
  tft_.fillScreen(TFT_BLACK);
  firstDashboard_ = (d.online || d.bootState == BOOT_CONNECTED);

  if (d.bootState == BOOT_SCANNING || d.bootState == BOOT_START) {
    TFT_eSprite page(&tft_);
    page.setColorDepth(16);
    page.createSprite(320,170);
    page.fillSprite(TFT_BLACK);

    FontGB2312::drawCenterString(page,160,7,"连接电池",TFT_CYAN,TFT_BLACK,2);
    FontGB2312::drawCenterString(page,160,43,
      d.statusMessage.length()?d.statusMessage:"扫描蓝牙...",
      TFT_WHITE,TFT_BLACK,1);

    int w=250;
    page.drawRoundRect(35,72,w,18,5,TFT_DARKGREY);
    int progress=(d.scanAttempt*100)/3;
    if(progress>100) progress=100;
    if(progress>0)
      page.fillRoundRect(38,75,(w-6)*progress/100,12,4,TFT_BLUE);

    FontGB2312::drawCenterString(page,160,96,
      String(d.scanAttempt)+"/3",TFT_YELLOW,TFT_BLACK,1);
    FontGB2312::drawCenterString(page,160,135,
      "自动扫描并连接JK保护板",TFT_LIGHTGREY,TFT_BLACK,1);
    page.pushSprite(0,0);
    page.deleteSprite();
    return;
  }

  if (d.bootState == BOOT_CONNECTING) {
    TFT_eSprite page(&tft_);
    page.setColorDepth(16);
    page.createSprite(320,170);
    page.fillSprite(TFT_BLACK);

    FontGB2312::drawCenterString(page,160,8,"正在连接",TFT_YELLOW,TFT_BLACK,2);
    FontGB2312::drawCenterString(page,160,50,
      d.mac.length()?d.mac:"JK-BMS",TFT_WHITE,TFT_BLACK,1);
    FontGB2312::drawCenterString(page,160,82,
      "正在建立蓝牙连接...",TFT_WHITE,TFT_BLACK,1);
    page.pushSprite(0,0);
    page.deleteSprite();
    return;
  }

  if ((d.bootState == BOOT_HOTSPOT || d.hotspot) && !d.online) {
    TFT_eSprite page(&tft_);
    page.setColorDepth(16);
    page.createSprite(320,170);
    page.fillSprite(TFT_BLACK);

    FontGB2312::drawCenterString(page,160,5,"热点设置",TFT_YELLOW,TFT_BLACK,2);
    FontGB2312::drawCenterString(page,160,43,
      "WiFi: JK-BMS-SETUP",TFT_WHITE,TFT_BLACK,1);
    FontGB2312::drawCenterString(page,160,70,
      "手机连接后打开网页",TFT_CYAN,TFT_BLACK,1);
    FontGB2312::drawCenterString(page,160,94,
      d.hotspotIp.length()?d.hotspotIp:"192.168.4.1",
      TFT_CYAN,TFT_BLACK,1);
    FontGB2312::drawCenterString(page,160,130,
      "扫描 / 选择 / 连接电池",TFT_LIGHTGREY,TFT_BLACK,1);
    page.pushSprite(0,0);
    page.deleteSprite();
    return;
  }

  drawDashboard(d, true);
}

void Display::drawDashboard(const BmsData& d, bool force) {
  if (force) {
    tft_.fillScreen(TFT_BLACK);
    drawStatus(d);
    drawSoc(d);
    drawVoltage(d);
    drawCurrent(d);
    drawPower(d);
    drawTemperature(d);
    drawRange(d);
    drawSocBar(d);
    return;
  }

  if (d.online != lastOnline_ || d.deviceName != lastDeviceName_)
    drawStatus(d);

  if (changed(d.soc,lastData_.soc,0.5f))
    drawSoc(d);

  if (changed(d.totalVoltage,lastData_.totalVoltage,0.1f))
    drawVoltage(d);

  if (changed(d.current,lastData_.current,0.1f))
    drawCurrent(d);

  if (changed(d.power,lastData_.power,1.0f))
    drawPower(d);

  if (changed(d.remainingCapacityAh,lastData_.remainingCapacityAh,0.1f) ||
      changed(d.temperature1,lastData_.temperature1,0.1f))
    drawTemperature(d);

  if (changed(d.remainingRangeKm,lastData_.remainingRangeKm,0.1f))
    drawRange(d);

  if (changed(d.soc,lastData_.soc,0.5f))
    drawSocBar(d);
}

void Display::drawStatus(const BmsData& d) {
  statusSprite_.fillSprite(TFT_BLACK);

  FontGB2312::drawText(statusSprite_,5,3,
    d.online ? "蓝牙已连接" : "蓝牙断开",
    d.online ? TFT_GREEN : TFT_LIGHTGREY,TFT_BLACK,1);

  if (d.deviceName.length()) {
    FontGB2312::drawRightString(statusSprite_,315,3,d.deviceName,
      TFT_LIGHTGREY,TFT_BLACK,1);
  }

  statusSprite_.pushSprite(0,0);
}

void Display::drawSoc(const BmsData& d) {
  socSprite_.fillSprite(TFT_BLACK);

  String value=String(d.soc,0);
  socSprite_.setTextColor(TFT_GREEN,TFT_BLACK);
  socSprite_.drawCentreString(value,68,2,7);

  socSprite_.setTextColor(TFT_GREEN,TFT_BLACK);
  socSprite_.drawString("%",105,43,2);

  socSprite_.pushSprite(0,21);
}

void Display::drawTemperature(const BmsData& d) {
  leftInfoSprite_.fillSprite(TFT_BLACK);

  FontGB2312::drawText(leftInfoSprite_,0,1,"容量",TFT_CYAN,TFT_BLACK,1);
  leftInfoSprite_.drawRightString(String(d.remainingCapacityAh,1)+"Ah",66,0,2);

  FontGB2312::drawText(leftInfoSprite_,72,1,"温度",TFT_YELLOW,TFT_BLACK,1);
  leftInfoSprite_.drawRightString(String(d.temperature1,1)+"C",139,0,2);

  leftInfoSprite_.pushSprite(0,89);
}

void Display::clearRow() {
  rowSprite_.fillSprite(TFT_BLACK);
}

void Display::drawVoltage(const BmsData& d) {
  clearRow();
  FontGB2312::drawText(rowSprite_,0,5,"电压",TFT_CYAN,TFT_BLACK,1);
  rowSprite_.drawRightString(String(d.totalVoltage,1)+" V",174,2,4);
  rowSprite_.pushSprite(145,24);
}

void Display::drawCurrent(const BmsData& d) {
  clearRow();
  FontGB2312::drawText(rowSprite_,0,5,"电流",TFT_YELLOW,TFT_BLACK,1);
  rowSprite_.drawRightString(String(d.current,1)+" A",174,2,4);
  rowSprite_.pushSprite(145,53);
}

void Display::drawPower(const BmsData& d) {
  clearRow();
  FontGB2312::drawText(rowSprite_,0,5,"功率",TFT_ORANGE,TFT_BLACK,1);
  rowSprite_.drawRightString(String(d.power,0)+" W",174,2,4);
  rowSprite_.pushSprite(145,82);
}

void Display::drawRange(const BmsData& d) {
  clearRow();
  FontGB2312::drawText(rowSprite_,0,5,"剩余里程",TFT_LIGHTGREY,TFT_BLACK,1);
  rowSprite_.drawRightString(String(d.remainingRangeKm,1)+" km",174,4,2);
  rowSprite_.pushSprite(145,111);
}

void Display::drawSocBar(const BmsData& d) {
  barSprite_.fillSprite(TFT_BLACK);

  float ratio=d.soc/100.0f;
  if(ratio<0) ratio=0;
  if(ratio>1) ratio=1;

  const int w=304;
  for(int i=0;i<16;i++) {
    float p0=i/16.0f;
    uint16_t color;

    if(p0<0.25f)
      color=lerp565(TFT_RED,TFT_ORANGE,(uint16_t)(p0*400));
    else if(p0<0.5f)
      color=lerp565(TFT_ORANGE,TFT_YELLOW,(uint16_t)((p0-0.25f)*400));
    else if(p0<0.75f)
      color=lerp565(TFT_YELLOW,TFT_GREEN,(uint16_t)((p0-0.5f)*400));
    else
      color=lerp565(TFT_GREEN,TFT_CYAN,(uint16_t)((p0-0.75f)*400));

    int sx=(w*i)/16;
    int sw=(w*(i+1))/16-(w*i)/16;

    if ((i+1)/16.0f<=ratio)
      barSprite_.fillRect(sx,0,sw,8,color);
    else
      barSprite_.drawRect(sx,0,sw,8,TFT_DARKGREY);
  }

  barSprite_.pushSprite(8,162);
}
