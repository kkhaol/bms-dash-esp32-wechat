#ifndef DISPLAY_H
#define DISPLAY_H

#include "../tft_setup.h"
#include <TFT_eSPI.h>
#include "BmsData.h"

class Display {
public:
  void begin();
  void update(const BmsData& d);

private:
  TFT_eSPI tft_;
  TFT_eSprite statusSprite_{&tft_};
  TFT_eSprite socSprite_{&tft_};
  TFT_eSprite leftInfoSprite_{&tft_};
  TFT_eSprite rowSprite_{&tft_};
  TFT_eSprite barSprite_{&tft_};

  bool initialized_ = false;
  bool firstDashboard_ = true;
  BmsBootState lastBootState_ = BOOT_START;
  bool lastOnline_ = false;
  String lastDeviceName_;

  BmsData lastData_{};

  void drawFullPage(const BmsData& d);
  void drawDashboard(const BmsData& d, bool force);
  void drawStatus(const BmsData& d);
  void drawSoc(const BmsData& d);
  void drawVoltage(const BmsData& d);
  void drawCurrent(const BmsData& d);
  void drawPower(const BmsData& d);
  void drawTemperature(const BmsData& d);
  void drawRange(const BmsData& d);
  void drawSocBar(const BmsData& d);

  bool changed(float a, float b, float eps) const;
  void clearRow();
};

#endif
