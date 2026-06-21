#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <math.h>
#include <string>
#include <vector>

// 1.9 inch ST7789 170x320 wiring.
static const int TFT_CS = 5;
static const int TFT_DC = 16;
static const int TFT_RST = 17;
static const int TFT_SCLK = 18;
static const int TFT_MOSI = 23;

// If the screen is mirrored, try 3 instead of 1.
static const uint8_t TFT_ROTATION = 1;
// If colors are inverted, change this to true/false.
static const bool TFT_INVERT = true;
// If the image is shifted or has a colored edge, enable custom offset and tune these.
static const bool TFT_USE_CUSTOM_OFFSET = false;
static const int8_t TFT_COL_OFFSET = 0;
static const int8_t TFT_ROW_OFFSET = 0;

static const uint16_t SCREEN_W = 320;
static const uint16_t SCREEN_H = 170;
static const uint32_t SERIAL_BAUD = 115200;
static const uint32_t BMS_SCAN_MS = 5000;
static const uint32_t STATUS_REQUEST_INTERVAL_MS = 1000;
static const uint32_t DASHBOARD_REFRESH_MS = 1000;
static const size_t MAX_FRAME_SIZE = 192;
static const char *PHONE_ADV_NAME = "BMS-DASH-SETUP";

// Nordic UART style service used by the WeChat mini program.
static const char *SETUP_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *SETUP_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *SETUP_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

// ESP32 NVS keys are limited to 15 chars.
static const char *NVS_SELECTED_BMS_MAC = "sel_bms_mac";
static const char *NVS_SELECTED_BMS_NAME = "sel_bms_name";

static NimBLEUUID ANT_BMS_SERVICE_UUID((uint16_t) 0xFFE0);
static NimBLEUUID ANT_BMS_CHARACTERISTIC_UUID((uint16_t) 0xFFE1);

struct BmsData {
  float voltage = NAN;
  float current = NAN;
  int soc = -1;
  float power = NAN;
  float mosTemp = NAN;
  int deltaCellVoltageMv = -1;
  float remainingCapacityAh = NAN;
  float maxCellVoltage = NAN;
  float minCellVoltage = NAN;
  bool valid = false;
};

struct BmsCandidate {
  String name;
  String mac;
  String serviceUuid;
  int rssi = -127;
  bool found = false;
};

enum AppState {
  BOOTING,
  BLE_SETUP_READY,
  TRY_CONNECT_LAST_BMS,
  BMS_CONNECTED,
  WAIT_CONFIG,
  SCANNING_BMS,
  SWITCHING_BMS,
  BMS_CONNECT_FAILED
};

class DashboardST7789 : public Adafruit_ST7789 {
public:
  using Adafruit_ST7789::Adafruit_ST7789;

  void setPanelOffset(int8_t col, int8_t row) {
    _colstart = col;
    _rowstart = row;
    _colstart2 = col;
    _rowstart2 = row;
  }
};

static DashboardST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
static U8G2_FOR_ADAFRUIT_GFX u8g2Text;
static Preferences preferences;

static NimBLEServer *setupServer = nullptr;
static NimBLECharacteristic *setupTxCharacteristic = nullptr;
static NimBLEClient *bleClient = nullptr;
static const NimBLEAdvertisedDevice *targetAdvertisedDevice = nullptr;
static NimBLERemoteCharacteristic *bmsCharacteristic = nullptr;
static std::vector<uint8_t> frameBuffer;
static QueueHandle_t phoneCommandQueue = nullptr;

static BmsData bmsData;
static BmsCandidate bestCandidate;
static std::vector<BmsCandidate> lastScanCandidates;
static String selectedBmsMac;
static String selectedBmsName;
static String preferredScanMac;
static String connectedName;
static String connectedMac;
static int connectedRssi = -127;
static bool connected = false;
static bool notifyReady = false;
static bool phoneConnected = false;
static bool phoneNotifySubscribed = false;
static uint16_t phoneConnHandle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t phoneMtu = 23;
static bool wasConnected = false;
static bool screenReady = false;
static AppState appState = BOOTING;
static uint32_t lastStatusRequestMs = 0;
static uint32_t lastDashboardDrawMs = 0;

struct PhoneCommand {
  char text[96];
};

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return tft.color565(r, g, b);
}

static uint16_t C_BG() { return rgb(3, 10, 16); }
static uint16_t C_PANEL() { return rgb(8, 20, 29); }
static uint16_t C_PANEL_2() { return rgb(12, 27, 38); }
static uint16_t C_BORDER() { return rgb(38, 65, 78); }
static uint16_t C_TEXT() { return rgb(230, 235, 240); }
static uint16_t C_MUTED() { return rgb(135, 145, 154); }
static uint16_t C_DIM() { return rgb(72, 82, 90); }
static uint16_t C_GREEN() { return rgb(25, 235, 92); }
static uint16_t C_YELLOW() { return rgb(240, 206, 45); }
static uint16_t C_RED() { return rgb(255, 47, 61); }
static uint16_t C_BLUE() { return rgb(0, 134, 255); }

static const char *appStateName(AppState state) {
  switch (state) {
    case BOOTING: return "BOOTING";
    case BLE_SETUP_READY: return "BLE_SETUP_READY";
    case TRY_CONNECT_LAST_BMS: return "TRY_CONNECT_LAST_BMS";
    case BMS_CONNECTED: return "BMS_CONNECTED";
    case WAIT_CONFIG: return "WAIT_CONFIG";
    case SCANNING_BMS: return "SCANNING_BMS";
    case SWITCHING_BMS: return "SWITCHING_BMS";
    case BMS_CONNECT_FAILED: return "BMS_CONNECT_FAILED";
    default: return "UNKNOWN";
  }
}

static void setAppState(AppState nextState, const char *reason = nullptr) {
  if (appState == nextState) {
    if (reason != nullptr) {
      Serial.printf("[STATE] %s: %s\r\n", appStateName(appState), reason);
    }
    return;
  }

  Serial.printf("[STATE] %s -> %s", appStateName(appState), appStateName(nextState));
  if (reason != nullptr) {
    Serial.printf(": %s", reason);
  }
  Serial.println();
  appState = nextState;
}

static void drawUtf8(const char *text, int16_t x, int16_t y, uint16_t color,
                     const uint8_t *font = u8g2_font_wqy14_t_gb2312) {
  u8g2Text.setFont(font);
  u8g2Text.setFontMode(1);
  u8g2Text.setForegroundColor(color);
  u8g2Text.drawUTF8(x, y, text);
}

static int16_t utf8Width(const char *text, const uint8_t *font = u8g2_font_wqy14_t_gb2312) {
  u8g2Text.setFont(font);
  return u8g2Text.getUTF8Width(text);
}

static void drawCenteredUtf8(const char *text, int16_t y, uint16_t color,
                             const uint8_t *font = u8g2_font_wqy16_t_gb2312) {
  int16_t w = utf8Width(text, font);
  drawUtf8(text, (SCREEN_W - w) / 2, y, color, font);
}

static void drawGfxText(const char *text, int16_t x, int16_t baseline, const GFXfont *font,
                        uint16_t color, uint8_t size = 1) {
  tft.setFont(font);
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(x, baseline);
  tft.print(text);
}

static int16_t gfxTextWidth(const char *text, const GFXfont *font, uint8_t size = 1) {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  tft.setFont(font);
  tft.setTextSize(size);
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return (int16_t) w;
}

static void resetFont() {
  tft.setFont(nullptr);
  tft.setTextSize(1);
}

static uint16_t getSocColor(int soc) {
  if (soc < 0) {
    return C_DIM();
  }
  if (soc < 10) {
    return C_RED();
  }
  if (soc <= 20) {
    return C_YELLOW();
  }
  return C_GREEN();
}

static String safeName(const String &name) {
  if (name.length() == 0) {
    return "(no name)";
  }
  return name;
}

static String normalizeMac(String mac) {
  mac.trim();
  mac.toUpperCase();
  return mac;
}

static bool macLooksValid(const String &mac) {
  if (mac.length() != 17) {
    return false;
  }
  for (uint8_t i = 0; i < mac.length(); i++) {
    const char c = mac[i];
    if ((i + 1) % 3 == 0) {
      if (c != ':') return false;
    } else if (!isxdigit((unsigned char) c)) {
      return false;
    }
  }
  return true;
}

static String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (uint16_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t) c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

static void appendFloatOrNull(String &json, float value, uint8_t decimals) {
  if (isnan(value)) {
    json += F("null");
  } else {
    json += String(value, (unsigned int) decimals);
  }
}

static void appendIntOrNull(String &json, int value) {
  if (value < 0) {
    json += F("null");
  } else {
    json += String(value);
  }
}

static String currentBmsName() {
  if (connectedName.length() > 0) {
    return connectedName;
  }
  if (selectedBmsName.length() > 0) {
    return selectedBmsName;
  }
  return "";
}

static String currentBmsMac() {
  if (connectedMac.length() > 0) {
    return connectedMac;
  }
  return selectedBmsMac;
}

static String buildStatusJson() {
  String json;
  json.reserve(220);
  json += F("{\"type\":\"status\",\"esp\":\"BMS-DASH\",\"bms_connected\":");
  json += connected ? F("true") : F("false");
  json += F(",\"state\":\"");
  json += appStateName(appState);
  json += F("\"");
  json += F(",\"selected_bms\":\"");
  json += jsonEscape(currentBmsMac());
  json += F("\",\"name\":\"");
  json += jsonEscape(currentBmsName());
  json += F("\",\"voltage\":");
  appendFloatOrNull(json, bmsData.valid ? bmsData.voltage : NAN, 1);
  json += F(",\"current\":");
  appendFloatOrNull(json, bmsData.valid ? bmsData.current : NAN, 1);
  json += F(",\"soc\":");
  appendIntOrNull(json, bmsData.valid ? bmsData.soc : -1);
  json += '}';
  return json;
}

static void sendSetupJson(String json) {
  if (!phoneConnected || !phoneNotifySubscribed || setupTxCharacteristic == nullptr) {
    Serial.printf("[SETUP-TX] Skip, phone not subscribed: %s\r\n", json.c_str());
    return;
  }

  if (!json.endsWith("\n")) {
    json += '\n';
  }

  const uint16_t payloadMtu = phoneMtu > 23 ? phoneMtu - 3 : 20;
  const uint16_t chunkSize = payloadMtu < 180 ? payloadMtu : 180;
  for (uint16_t offset = 0; offset < json.length(); offset += chunkSize) {
    const uint16_t remaining = json.length() - offset;
    const uint16_t len = remaining < chunkSize ? remaining : chunkSize;
    setupTxCharacteristic->notify((const uint8_t *) json.c_str() + offset, len, phoneConnHandle);
    delay(12);
  }
  Serial.printf("[SETUP-TX] %s", json.c_str());
}

static void sendSetupError(const String &message) {
  String json = F("{\"type\":\"error\",\"message\":\"");
  json += jsonEscape(message);
  json += F("\"}");
  sendSetupJson(json);
}

static void saveSelectedBms() {
  preferences.putString(NVS_SELECTED_BMS_MAC, selectedBmsMac);
  preferences.putString(NVS_SELECTED_BMS_NAME, selectedBmsName);
  Serial.printf("[NVS] Saved selected BMS mac=%s name=\"%s\"\r\n",
                selectedBmsMac.c_str(), selectedBmsName.c_str());
}

static void fillBackground() {
  tft.fillScreen(C_BG());
  tft.fillRoundRect(8, 6, 304, 158, 18, rgb(4, 13, 20));
  tft.drawRoundRect(8, 6, 304, 158, 18, rgb(28, 37, 42));
  tft.fillRoundRect(18, 16, 284, 138, 14, C_PANEL());
}

static void drawBluetoothIcon(bool isConnected) {
  uint16_t c = isConnected ? C_BLUE() : C_DIM();
  int x = 288;
  int y = 18;
  tft.drawLine(x, y - 12, x, y + 12, c);
  tft.drawLine(x, y - 12, x + 10, y - 3, c);
  tft.drawLine(x + 10, y - 3, x, y + 6, c);
  tft.drawLine(x, y + 6, x + 10, y + 12, c);
  tft.drawLine(x + 10, y + 12, x, y + 2, c);
  tft.drawLine(x, y + 2, x - 8, y + 10, c);
  tft.drawLine(x, y - 2, x - 8, y - 10, c);
  tft.drawLine(x - 8, y - 10, x, y + 2, c);
  tft.drawLine(x - 8, y + 10, x, y - 2, c);
}

static void drawThermometerIcon(int x, int y, uint16_t c) {
  tft.drawRoundRect(x, y, 6, 17, 3, c);
  tft.fillCircle(x + 3, y + 18, 5, C_BG());
  tft.drawCircle(x + 3, y + 18, 5, c);
  tft.fillCircle(x + 3, y + 18, 2, c);
}

static void drawSocBar(int soc) {
  const int x = 100;
  const int y = 15;
  const int w = 120;
  const int h = 12;
  tft.fillRoundRect(x, y, w, h, 4, rgb(10, 31, 30));
  tft.drawRoundRect(x, y, w, h, 4, rgb(93, 112, 115));
  if (soc >= 0) {
    int fillW = constrain(map(soc, 0, 100, 0, w - 4), 0, w - 4);
    if (fillW > 0) {
      tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, 3, getSocColor(soc));
    }
  }
}

static void drawSignalBars(int x, int y, int rssi, uint16_t c) {
  int bars = 1;
  if (rssi > -80) bars = 2;
  if (rssi > -68) bars = 3;
  if (rssi > -55) bars = 4;
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 3;
    int bx = x + i * 5;
    uint16_t bc = (i < bars) ? c : C_DIM();
    tft.fillRect(bx, y - h, 3, h, bc);
  }
}

static void drawWaveIcon(int x, int y, uint16_t c) {
  tft.drawCircle(x + 10, y + 10, 10, c);
  tft.drawLine(x + 4, y + 10, x + 8, y + 6, c);
  tft.drawLine(x + 8, y + 6, x + 12, y + 14, c);
  tft.drawLine(x + 12, y + 14, x + 16, y + 8, c);
}

static void drawBatteryIcon(int x, int y, uint16_t c) {
  tft.drawRoundRect(x, y, 15, 21, 2, c);
  tft.fillRect(x + 5, y - 3, 5, 3, c);
  tft.drawFastHLine(x + 4, y + 6, 7, c);
  tft.drawFastHLine(x + 4, y + 11, 7, c);
  tft.drawFastHLine(x + 4, y + 16, 7, c);
}

static void drawStatusDots(int y, uint16_t active) {
  uint8_t phase = (millis() / 350) % 4;
  for (int i = 0; i < 4; i++) {
    tft.fillCircle(136 + i * 16, y, 4, i == phase ? active : C_DIM());
  }
}

static String shortText(String value, uint8_t maxLen) {
  value.trim();
  if (value.length() <= maxLen) {
    return value;
  }
  return value.substring(0, maxLen - 3) + "...";
}

static void drawAsciiCentered(const String &text, int16_t y, uint16_t color) {
  resetFont();
  tft.setTextSize(1);
  tft.setTextColor(color);
  const int16_t x = (SCREEN_W - int16_t(text.length()) * 6) / 2;
  tft.setCursor(x < 20 ? 20 : x, y);
  tft.print(text);
}

static void drawStateChip(const char *state, uint16_t accent) {
  resetFont();
  tft.fillRoundRect(36, 24, 150, 18, 6, rgb(10, 25, 35));
  tft.drawRoundRect(36, 24, 150, 18, 6, accent);
  tft.setTextColor(accent);
  tft.setTextSize(1);
  tft.setCursor(44, 30);
  tft.print(state);
}

static void drawStateShell(const char *title, uint16_t accent, bool activeDots) {
  fillBackground();
  tft.fillRoundRect(23, 26, 6, 108, 3, accent);
  drawBluetoothIcon(phoneConnected);
  drawStateChip(appStateName(appState), accent);
  if (activeDots) {
    drawStatusDots(60, accent);
  }
  drawCenteredUtf8(title, 82, C_TEXT(), u8g2_font_wqy16_t_gb2312);
}

static void drawStateHint(const char *line1, const char *line2) {
  drawCenteredUtf8(line1, 108, C_MUTED(), u8g2_font_wqy14_t_gb2312);
  drawCenteredUtf8(line2, 130, C_MUTED(), u8g2_font_wqy14_t_gb2312);
}

static void drawBootScreen() {
  drawStateShell("启动中", C_BLUE(), true);
  drawStateHint("初始化屏幕和蓝牙", "读取已保存 BMS");
}

static void drawBleReadyScreen() {
  drawStateShell("配置蓝牙已开启", C_BLUE(), false);
  drawStateHint("微信可连接仪表", PHONE_ADV_NAME);
}

static void drawWaitConfigScreen(const char *reason) {
  drawStateShell("等待手机配置", C_YELLOW(), false);
  drawStateHint("打开微信小程序", "选择或重连 BMS");
  drawAsciiCentered(String(reason), 145, C_DIM());
}

static void drawTryLastBmsScreen() {
  drawStateShell("连接上次 BMS", C_BLUE(), true);
  const String name = selectedBmsName.length() ? selectedBmsName : "Saved BMS";
  drawAsciiCentered(shortText(name, 24), 108, C_MUTED());
  drawAsciiCentered(selectedBmsMac, 124, C_TEXT());
  drawAsciiCentered("No strongest fallback", 145, C_DIM());
}

static void drawManualBmsScanScreen() {
  drawStateShell("扫描 BMS 中", C_GREEN(), true);
  drawStateHint("小程序发起扫描", "发现后返回手机");
  drawAsciiCentered("Scan window: 5s", 145, C_DIM());
}

static void drawSwitchingBmsScreen(const String &mac) {
  drawStateShell("切换 BMS 中", C_YELLOW(), true);
  drawStateHint("正在连接新目标", "成功后才保存");
  drawAsciiCentered(mac, 145, C_TEXT());
}

static void drawConnectFailedScreen(const char *reason) {
  drawStateShell("BMS 连接失败", C_RED(), false);
  drawStateHint("原保存配置不变", "等待手机重新选择");
  drawAsciiCentered(String(reason), 145, C_DIM());
}

static void drawScanScreen() {
  if (appState == TRY_CONNECT_LAST_BMS) {
    drawTryLastBmsScreen();
  } else if (appState == SWITCHING_BMS) {
    drawSwitchingBmsScreen(preferredScanMac);
  } else {
    drawManualBmsScanScreen();
  }
  screenReady = true;
}

static void drawConnectingScreen(const String &deviceName, int rssi) {
  drawStateShell("正在连接 BMS", C_BLUE(), true);
  tft.drawCircle(160, 60, 32, C_DIM());
  tft.drawLine(160, 38, 160, 82, C_TEXT());
  tft.drawLine(160, 38, 177, 52, C_TEXT());
  tft.drawLine(177, 52, 160, 68, C_TEXT());
  tft.drawLine(160, 68, 177, 82, C_TEXT());
  tft.drawLine(177, 82, 160, 56, C_TEXT());
  tft.drawLine(160, 56, 143, 74, C_TEXT());
  tft.drawLine(160, 64, 143, 46, C_TEXT());
  String line = safeName(deviceName);
  drawAsciiCentered(shortText(line, 24), 120, C_MUTED());
  char rssiText[18];
  snprintf(rssiText, sizeof(rssiText), "RSSI %d dBm", rssi);
  drawAsciiCentered(String(rssiText), 140, C_TEXT());
}

static void drawMessageScreen(const char *line1, const char *line2) {
  fillBackground();
  drawBluetoothIcon(false);
  drawCenteredUtf8(line1, 76, C_TEXT(), u8g2_font_wqy16_t_gb2312);
  drawCenteredUtf8(line2, 104, C_MUTED(), u8g2_font_wqy14_t_gb2312);
}

static void drawValueWithUnit(const char *number, const char *unit, int x, int baseline,
                              const GFXfont *numberFont, const GFXfont *unitFont,
                              uint16_t color, int unitGap = 3) {
  drawGfxText(number, x, baseline, numberFont, color);
  int16_t w = gfxTextWidth(number, numberFont);
  drawGfxText(unit, x + w + unitGap, baseline, unitFont, color);
}

static void drawMainDashboard(const BmsData &data) {
  fillBackground();
  drawBluetoothIcon(true);

  drawThermometerIcon(31, 25, C_TEXT());
  if (!isnan(data.mosTemp)) {
    char tempText[12];
    snprintf(tempText, sizeof(tempText), "%.0f", data.mosTemp);
    drawGfxText(tempText, 45, 46, &FreeSans9pt7b, C_TEXT());
    int tx = 45 + gfxTextWidth(tempText, &FreeSans9pt7b) + 4;
    tft.drawCircle(tx + 3, 34, 2, C_TEXT());
    drawGfxText("C", tx + 8, 46, &FreeSans9pt7b, C_TEXT());
  } else {
    drawGfxText("--", 45, 46, &FreeSans9pt7b, C_TEXT());
  }

  drawSocBar(data.soc);

  char socText[8];
  if (data.soc >= 0) {
    snprintf(socText, sizeof(socText), "%d", data.soc);
  } else {
    snprintf(socText, sizeof(socText), "--");
  }
  int16_t socW = gfxTextWidth(socText, &FreeSansBold24pt7b);
  int socX = 160 - (socW + 30) / 2;
  drawGfxText(socText, socX, 97, &FreeSansBold24pt7b, C_TEXT());
  drawGfxText("%", socX + socW + 4, 94, &FreeSansBold18pt7b, C_TEXT());
  drawGfxText("SOC", 140, 127, &FreeSansBold9pt7b, C_MUTED());

  char voltText[12];
  char currText[12];
  if (!isnan(data.voltage)) {
    snprintf(voltText, sizeof(voltText), "%.1f", data.voltage);
  } else {
    snprintf(voltText, sizeof(voltText), "--");
  }
  if (!isnan(data.current)) {
    snprintf(currText, sizeof(currText), "%.1f", data.current);
  } else {
    snprintf(currText, sizeof(currText), "--");
  }
  drawValueWithUnit(voltText, "V", 31, 83, &FreeSansBold18pt7b, &FreeSansBold12pt7b, C_TEXT());
  tft.drawFastHLine(34, 96, 58, C_DIM());
  drawValueWithUnit(currText, "A", 31, 123, &FreeSansBold18pt7b, &FreeSansBold12pt7b, C_TEXT());

  char powerText[12];
  if (!isnan(data.power)) {
    snprintf(powerText, sizeof(powerText), "%.0f", data.power);
  } else {
    snprintf(powerText, sizeof(powerText), "--");
  }
  int pX = 236;
  drawGfxText(powerText, pX, 103, &FreeSansBold24pt7b, C_RED());
  int16_t pW = gfxTextWidth(powerText, &FreeSansBold24pt7b);
  drawGfxText("w", pX + pW + 3, 103, &FreeSansBold12pt7b, C_RED());

  tft.fillRoundRect(30, 137, 119, 26, 9, C_PANEL_2());
  tft.drawRoundRect(30, 137, 119, 26, 9, C_BORDER());
  drawWaveIcon(40, 140, C_GREEN());
  drawUtf8("压差", 67, 156, C_MUTED(), u8g2_font_wqy14_t_gb2312);
  char deltaText[18];
  if (data.deltaCellVoltageMv >= 0) {
    snprintf(deltaText, sizeof(deltaText), "%dmV", data.deltaCellVoltageMv);
  } else {
    snprintf(deltaText, sizeof(deltaText), "--");
  }
  drawGfxText(deltaText, 104, 156, &FreeSansBold9pt7b, C_TEXT());

  tft.fillRoundRect(171, 137, 119, 26, 9, C_PANEL_2());
  tft.drawRoundRect(171, 137, 119, 26, 9, C_BORDER());
  drawBatteryIcon(188, 141, C_GREEN());
  drawUtf8("剩余", 211, 156, C_MUTED(), u8g2_font_wqy14_t_gb2312);
  char capText[18];
  if (!isnan(data.remainingCapacityAh)) {
    snprintf(capText, sizeof(capText), "%.1fAh", data.remainingCapacityAh);
  } else {
    snprintf(capText, sizeof(capText), "--");
  }
  drawGfxText(capText, 249, 156, &FreeSansBold9pt7b, C_TEXT());
}

static uint16_t crc16(const uint8_t *data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x01) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

static uint16_t getU16LE(const std::vector<uint8_t> &data, size_t index) {
  return (uint16_t(data[index + 1]) << 8) | uint16_t(data[index]);
}

static uint32_t getU32LE(const std::vector<uint8_t> &data, size_t index) {
  return (uint32_t(data[index + 3]) << 24) |
         (uint32_t(data[index + 2]) << 16) |
         (uint32_t(data[index + 1]) << 8) |
         uint32_t(data[index]);
}

static void printHex(const char *prefix, const uint8_t *data, size_t length) {
  Serial.print(prefix);
  for (size_t i = 0; i < length; i++) {
    if (i > 0) {
      Serial.print(' ');
    }
    if (data[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
  }
  Serial.println();
}

static void buildCommandFrame(uint8_t function, uint16_t address, uint8_t value, uint8_t frame[10]) {
  frame[0] = 0x7E;
  frame[1] = 0xA1;
  frame[2] = function;
  frame[3] = address & 0xFF;
  frame[4] = address >> 8;
  frame[5] = value;
  uint16_t crc = crc16(frame + 1, 5);
  frame[6] = crc & 0xFF;
  frame[7] = crc >> 8;
  frame[8] = 0xAA;
  frame[9] = 0x55;
}

static bool sendCommand(uint8_t function, uint16_t address, uint8_t value, const char *label) {
  if (!connected || !notifyReady || bmsCharacteristic == nullptr) {
    Serial.printf("[TX] Skip %s: BLE not ready\r\n", label);
    return false;
  }
  uint8_t frame[10];
  buildCommandFrame(function, address, value, frame);
  printHex("[TX] ", frame, sizeof(frame));
  bmsCharacteristic->writeValue(frame, sizeof(frame), false);
  Serial.printf("[TX] %s sent\r\n", label);
  return true;
}

static void sendDeviceInfoRequest() {
  sendCommand(0x02, 0x026C, 0x20, "device info request");
}

static void sendStatusRequest() {
  sendCommand(0x01, 0x0000, 0xBE, "status request");
}

static void parseStatusFrame(const std::vector<uint8_t> &frame) {
  if (frame.size() < 10 || frame[0] != 0x7E || frame[1] != 0xA1 || frame[2] != 0x11) {
    return;
  }

  const uint8_t dataLen = frame[5];
  const size_t expectedLen = 6 + dataLen + 4;
  if (frame.size() != expectedLen) {
    Serial.printf("[PARSE] Status frame length mismatch: got=%u expected=%u\r\n",
                  (unsigned) frame.size(), (unsigned) expectedLen);
    return;
  }

  const uint16_t computedCrc = crc16(frame.data() + 1, expectedLen - 5);
  const uint16_t receivedCrc = (uint16_t(frame[expectedLen - 3]) << 8) | frame[expectedLen - 4];
  Serial.printf("[PARSE] CRC %s computed=0x%04X received=0x%04X\r\n",
                computedCrc == receivedCrc ? "OK" : "FAIL", computedCrc, receivedCrc);
  if (computedCrc != receivedCrc) {
    return;
  }

  const uint8_t temperatureSensors = frame[8];
  const uint8_t cells = frame[9];
  if (cells == 0 || cells > 32 || temperatureSensors > 6) {
    Serial.printf("[PARSE] Unexpected cell/temp count: cells=%u temp_sensors=%u\r\n",
                  cells, temperatureSensors);
    return;
  }

  const size_t offset = size_t(cells) * 2 + size_t(temperatureSensors) * 2;
  const size_t mosTemperatureIndex = 34 + offset;
  const size_t voltageIndex = 38 + offset;
  const size_t currentIndex = 40 + offset;
  const size_t socIndex = 42 + offset;
  const size_t remainingCapacityIndex = 54 + offset;
  const size_t powerIndex = 62 + offset;
  const size_t maxCellVoltageIndex = 74 + offset;
  const size_t minCellVoltageIndex = 78 + offset;
  const size_t deltaCellVoltageIndex = 82 + offset;

  const size_t requiredLastIndex = max(deltaCellVoltageIndex + 1, max(powerIndex + 3, remainingCapacityIndex + 3));
  if (requiredLastIndex >= frame.size()) {
    Serial.printf("[PARSE] Status frame too short for dashboard fields: len=%u\r\n",
                  (unsigned) frame.size());
    return;
  }

  BmsData next;
  next.voltage = getU16LE(frame, voltageIndex) * 0.01f;
  next.current = int16_t(getU16LE(frame, currentIndex)) * 0.1f;
  next.soc = int16_t(getU16LE(frame, socIndex));
  next.mosTemp = int16_t(getU16LE(frame, mosTemperatureIndex));
  next.remainingCapacityAh = getU32LE(frame, remainingCapacityIndex) * 0.000001f;
  next.maxCellVoltage = getU16LE(frame, maxCellVoltageIndex) * 0.001f;
  next.minCellVoltage = getU16LE(frame, minCellVoltageIndex) * 0.001f;
  next.deltaCellVoltageMv = int(roundf(getU16LE(frame, deltaCellVoltageIndex) * 1.0f));

  const float realPower = float(int32_t(getU32LE(frame, powerIndex)));
  const float computedPower = next.voltage * fabsf(next.current);
  next.power = fabsf(realPower) > 0.5f ? fabsf(realPower) : computedPower;
  next.valid = true;
  bmsData = next;

  Serial.printf("[DATA] name=%s mac=%s rssi=%d voltage=%.2fV current=%.1fA soc=%d%% power=%.0fw mosTemp=%.0fC delta=%dmV remaining=%.1fAh maxCell=%.3fV minCell=%.3fV\r\n",
                connectedName.c_str(), connectedMac.c_str(), connectedRssi,
                next.voltage, next.current, next.soc, next.power, next.mosTemp,
                next.deltaCellVoltageMv, next.remainingCapacityAh,
                next.maxCellVoltage, next.minCellVoltage);
}

static void handleCompleteFrame(const std::vector<uint8_t> &frame) {
  printHex("[RX-FRAME] ", frame.data(), frame.size());
  if (frame.size() < 10) {
    Serial.println("[PARSE] Frame too short");
    return;
  }
  if (frame[2] == 0x11) {
    parseStatusFrame(frame);
  } else {
    Serial.printf("[PARSE] Non-status frame received: function=0x%02X len=%u\r\n",
                  frame[2], (unsigned) frame.size());
  }
}

static void bmsNotifyCallback(NimBLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool isNotify) {
  (void) characteristic;
  (void) isNotify;
  printHex("[RX-CHUNK] ", data, length);

  if (length >= 2 && data[0] == 0x7E && data[1] == 0xA1) {
    frameBuffer.clear();
  }
  frameBuffer.insert(frameBuffer.end(), data, data + length);

  if (frameBuffer.size() > MAX_FRAME_SIZE) {
    Serial.printf("[RX] Frame buffer exceeded %u bytes, clearing\r\n", (unsigned) MAX_FRAME_SIZE);
    frameBuffer.clear();
    return;
  }

  if (frameBuffer.size() >= 10 &&
      frameBuffer[frameBuffer.size() - 2] == 0xAA &&
      frameBuffer[frameBuffer.size() - 1] == 0x55) {
    handleCompleteFrame(frameBuffer);
    frameBuffer.clear();
  }
}

class BmsClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *client) override {
    (void) client;
    connected = true;
    Serial.println("[CONNECT] BLE client connected");
  }

  void onDisconnect(NimBLEClient *client, int reason) override {
    (void) client;
    connected = false;
    notifyReady = false;
    bmsCharacteristic = nullptr;
    frameBuffer.clear();
    Serial.printf("[CONNECT] BLE client disconnected reason=%d\r\n", reason);
  }
} bmsClientCallbacks;

class SetupServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
    phoneConnected = true;
    phoneConnHandle = connInfo.getConnHandle();
    phoneMtu = 23;
    server->updateConnParams(phoneConnHandle, 24, 48, 0, 180);
    Serial.printf("[SETUP] Phone connected: %s handle=%u\r\n",
                  connInfo.getAddress().toString().c_str(), phoneConnHandle);
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override {
    (void) server;
    Serial.printf("[SETUP] Phone disconnected: %s reason=%d\r\n",
                  connInfo.getAddress().toString().c_str(), reason);
    phoneConnected = false;
    phoneNotifySubscribed = false;
    phoneConnHandle = BLE_HS_CONN_HANDLE_NONE;
    phoneMtu = 23;
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo &connInfo) override {
    phoneMtu = mtu;
    phoneConnHandle = connInfo.getConnHandle();
    Serial.printf("[SETUP] Phone MTU=%u handle=%u\r\n", phoneMtu, phoneConnHandle);
  }
} setupServerCallbacks;

class SetupTxCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo, uint16_t subValue) override {
    (void) characteristic;
    phoneConnHandle = connInfo.getConnHandle();
    phoneNotifySubscribed = (subValue & 0x0001) != 0;
    Serial.printf("[SETUP] TX notify %s handle=%u\r\n",
                  phoneNotifySubscribed ? "subscribed" : "unsubscribed", phoneConnHandle);
  }
} setupTxCallbacks;

class SetupRxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
    (void) connInfo;
    if (phoneCommandQueue == nullptr) {
      return;
    }

    const std::string value = characteristic->getValue();
    PhoneCommand command = {};
    const size_t copyLen = value.length() < sizeof(command.text) - 1
                             ? value.length()
                             : sizeof(command.text) - 1;
    memcpy(command.text, value.data(), copyLen);
    command.text[copyLen] = '\0';

    if (xQueueSend(phoneCommandQueue, &command, 0) != pdTRUE) {
      Serial.println("[SETUP-RX] Command queue full");
    } else {
      Serial.printf("[SETUP-RX] %s\r\n", command.text);
    }
  }
} setupRxCallbacks;

static bool stringContainsIgnoreCase(String value, const char *token) {
  value.toUpperCase();
  String needle(token);
  needle.toUpperCase();
  return value.indexOf(needle) >= 0;
}

static bool isBmsCandidate(const String &name, const String &serviceUuid) {
  if (stringContainsIgnoreCase(name, "ANT") || stringContainsIgnoreCase(name, "BMS")) {
    return true;
  }
  if (stringContainsIgnoreCase(serviceUuid, "0000ffe0") || stringContainsIgnoreCase(serviceUuid, "ffe0")) {
    return true;
  }
  return false;
}

static void resetScanState() {
  bestCandidate = BmsCandidate();
  targetAdvertisedDevice = nullptr;
  lastScanCandidates.clear();
}

static String firstServiceUuid(const NimBLEAdvertisedDevice *advertisedDevice) {
  if (advertisedDevice->getServiceUUIDCount() == 0) {
    return "";
  }
  return advertisedDevice->getServiceUUID(0).toString().c_str();
}

static void rememberScanCandidate(const BmsCandidate &candidate) {
  for (BmsCandidate &existing : lastScanCandidates) {
    if (existing.mac.equalsIgnoreCase(candidate.mac)) {
      if (candidate.rssi > existing.rssi) {
        existing = candidate;
      }
      return;
    }
  }
  lastScanCandidates.push_back(candidate);
}

static bool findLastScanCandidate(const String &mac, BmsCandidate &candidate) {
  for (const BmsCandidate &item : lastScanCandidates) {
    if (item.mac.equalsIgnoreCase(mac)) {
      candidate = item;
      return true;
    }
  }
  return false;
}

class BmsScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    String mac = normalizeMac(advertisedDevice->getAddress().toString().c_str());
    String name = advertisedDevice->haveName() ? advertisedDevice->getName().c_str() : "";
    name.trim();
    String serviceUuid = firstServiceUuid(advertisedDevice);
    int rssi = advertisedDevice->getRSSI();

    Serial.printf("[SCAN] Name=\"%s\" MAC=%s RSSI=%d", name.c_str(), mac.c_str(), rssi);
    if (serviceUuid.length() > 0) {
      Serial.printf(" ServiceUUID=%s", serviceUuid.c_str());
    }
    Serial.println();

    if (!isBmsCandidate(name, serviceUuid)) {
      return;
    }

    Serial.printf("[SCAN] Candidate BMS: name=\"%s\" mac=%s rssi=%d uuid=%s\r\n",
                  name.c_str(), mac.c_str(), rssi, serviceUuid.c_str());

    BmsCandidate candidate;
    candidate.name = safeName(name);
    candidate.mac = mac;
    candidate.serviceUuid = serviceUuid;
    candidate.rssi = rssi;
    candidate.found = true;
    rememberScanCandidate(candidate);

    if (preferredScanMac.length() > 0 && mac.equalsIgnoreCase(preferredScanMac)) {
      targetAdvertisedDevice = advertisedDevice;
      bestCandidate = candidate;
      Serial.printf("[SCAN] Preferred BMS found: name=\"%s\" mac=%s rssi=%d\r\n",
                    bestCandidate.name.c_str(), bestCandidate.mac.c_str(), bestCandidate.rssi);
      advertisedDevice->getScan()->stop();
      return;
    }

    if (!bestCandidate.found || rssi > bestCandidate.rssi) {
      targetAdvertisedDevice = advertisedDevice;
      bestCandidate = candidate;
      Serial.printf("[SCAN] Best candidate updated: name=\"%s\" mac=%s rssi=%d\r\n",
                    bestCandidate.name.c_str(), bestCandidate.mac.c_str(), bestCandidate.rssi);
    }
  }
} bmsScanCallbacks;

static BmsCandidate scanForBms(const String &preferredMac) {
  resetScanState();
  preferredScanMac = normalizeMac(preferredMac);
  drawScanScreen();

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&bmsScanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->setMaxResults(0xFF);

  Serial.printf("[SCAN] Scanning for %u ms. Preferred MAC=%s\r\n",
                (unsigned) BMS_SCAN_MS,
                preferredScanMac.length() ? preferredScanMac.c_str() : "(none)");
  scan->getResults(BMS_SCAN_MS, false);

  if (bestCandidate.found) {
    Serial.printf("[SCAN] Selected BMS candidate: name=\"%s\" mac=%s rssi=%d uuid=%s\r\n",
                  bestCandidate.name.c_str(), bestCandidate.mac.c_str(),
                  bestCandidate.rssi, bestCandidate.serviceUuid.c_str());
  } else {
    Serial.println("[SCAN] No BMS candidate found");
  }
  return bestCandidate;
}

static void disconnectAndCleanup() {
  notifyReady = false;
  connected = false;
  bmsCharacteristic = nullptr;
  frameBuffer.clear();
  connectedName = "";
  connectedMac = "";
  connectedRssi = -127;
  bmsData = BmsData();
  if (bleClient != nullptr) {
    if (bleClient->isConnected()) {
      bleClient->disconnect();
    }
    NimBLEDevice::deleteClient(bleClient);
    bleClient = nullptr;
  }
}

static bool connectToBms() {
  if (!bestCandidate.found || targetAdvertisedDevice == nullptr) {
    Serial.println("[CONNECT] No target device available");
    return false;
  }

  drawConnectingScreen(bestCandidate.name, bestCandidate.rssi);
  Serial.printf("[CONNECT] Connecting to name=\"%s\" mac=%s rssi=%d\r\n",
                bestCandidate.name.c_str(), bestCandidate.mac.c_str(), bestCandidate.rssi);

  disconnectAndCleanup();
  bleClient = NimBLEDevice::createClient();
  bleClient->setClientCallbacks(&bmsClientCallbacks, false);
  bleClient->setConnectionParams(12, 24, 0, 150);
  bleClient->setConnectTimeout(8000);

  if (!bleClient->connect(targetAdvertisedDevice)) {
    Serial.println("[CONNECT] Failed to connect");
    disconnectAndCleanup();
    return false;
  }

  NimBLERemoteService *service = bleClient->getService(ANT_BMS_SERVICE_UUID);
  if (service == nullptr) {
    Serial.println("[CONNECT] Service 0xFFE0 not found");
    disconnectAndCleanup();
    return false;
  }
  Serial.println("[CONNECT] Service 0xFFE0 found");

  bmsCharacteristic = service->getCharacteristic(ANT_BMS_CHARACTERISTIC_UUID);
  if (bmsCharacteristic == nullptr) {
    Serial.println("[CONNECT] Characteristic 0xFFE1 not found");
    disconnectAndCleanup();
    return false;
  }
  Serial.println("[CONNECT] Characteristic 0xFFE1 found");

  if (!bmsCharacteristic->canNotify()) {
    Serial.println("[CONNECT] Characteristic 0xFFE1 does not support notify");
    disconnectAndCleanup();
    return false;
  }

  if (!bmsCharacteristic->subscribe(true, bmsNotifyCallback, true)) {
    Serial.println("[CONNECT] Subscribe 0xFFE1 notify failed");
    disconnectAndCleanup();
    return false;
  }

  notifyReady = true;
  connected = true;
  wasConnected = true;
  connectedName = bestCandidate.name;
  connectedMac = bestCandidate.mac;
  connectedRssi = bestCandidate.rssi;

  Serial.println("[CONNECT] Notify subscribed: connection ready");
  Serial.printf("[CONNECT] Connected BMS: name=\"%s\" mac=%s\r\n",
                connectedName.c_str(), connectedMac.c_str());

  sendDeviceInfoRequest();
  delay(300);
  sendStatusRequest();
  lastStatusRequestMs = millis();
  lastDashboardDrawMs = 0;
  setAppState(BMS_CONNECTED, "BMS connection ready");
  drawMainDashboard(bmsData);
  NimBLEDevice::getScan()->clearResults();
  return true;
}

static void notifyScanResultsToPhone() {
  for (const BmsCandidate &candidate : lastScanCandidates) {
    String json;
    json.reserve(128);
    json += F("{\"type\":\"bms_found\",\"name\":\"");
    json += jsonEscape(candidate.name);
    json += F("\",\"mac\":\"");
    json += jsonEscape(candidate.mac);
    json += F("\",\"rssi\":");
    json += String(candidate.rssi);
    json += '}';
    sendSetupJson(json);
  }
}

static void enterWaitConfig(const char *line1, const char *line2, const char *reason) {
  (void) line1;
  (void) line2;
  setAppState(WAIT_CONFIG, reason);
  drawWaitConfigScreen(reason);
}

static void enterConnectFailed(const char *reason, bool reportToPhone) {
  setAppState(BMS_CONNECT_FAILED, reason);
  drawConnectFailedScreen(reason);
  if (reportToPhone) {
    sendSetupError("connect_failed");
  }
  delay(1200);
  setAppState(WAIT_CONFIG, "waiting for mini program config");
  drawWaitConfigScreen("connect failed");
}

static bool connectSavedBms(bool reportToPhone) {
  if (selectedBmsMac.length() == 0) {
    Serial.println("[POLICY] No selected_bms_mac in NVS; waiting for mini program config");
    enterWaitConfig("NO BMS SAVED", "WAIT CONFIG", "no saved BMS");
    if (reportToPhone) {
      sendSetupError("no_saved_bms");
    }
    return false;
  }

  setAppState(TRY_CONNECT_LAST_BMS, "scan only saved BMS MAC");
  BmsCandidate candidate = scanForBms(selectedBmsMac);
  const bool savedFound = candidate.found && candidate.mac.equalsIgnoreCase(selectedBmsMac);

  if (!savedFound) {
    NimBLEDevice::getScan()->clearResults();
    Serial.printf("[POLICY] Saved BMS %s not found; no strongest fallback\r\n", selectedBmsMac.c_str());
    enterWaitConfig("BMS NOT FOUND", "WAIT CONFIG", "saved BMS not found");
    if (reportToPhone) {
      sendSetupError("saved_bms_not_found");
    }
    return false;
  }

  if (!connectToBms()) {
    NimBLEDevice::getScan()->clearResults();
    Serial.printf("[POLICY] Saved BMS %s connect failed; saved config unchanged\r\n", selectedBmsMac.c_str());
    enterConnectFailed("saved BMS connect failed", reportToPhone);
    return false;
  }

  if (selectedBmsName != connectedName) {
    selectedBmsName = connectedName;
    saveSelectedBms();
  }

  if (reportToPhone) {
    sendSetupJson(buildStatusJson());
  }
  return true;
}

static void handleManualScanCommand() {
  setAppState(SCANNING_BMS, "mini program requested SCAN_BMS");
  sendSetupJson(F("{\"type\":\"scan_start\"}"));
  scanForBms("");
  notifyScanResultsToPhone();
  sendSetupJson(F("{\"type\":\"scan_done\"}"));
  NimBLEDevice::getScan()->clearResults();
  if (connected) {
    setAppState(BMS_CONNECTED, "manual scan done");
    drawMainDashboard(bmsData);
  } else {
    setAppState(WAIT_CONFIG, "manual scan done");
    drawWaitConfigScreen("select BMS in mini program");
  }
}

static void handleSelectBmsCommand(String mac) {
  mac = normalizeMac(mac);
  if (!macLooksValid(mac)) {
    sendSetupError("invalid MAC");
    return;
  }

  if (connected && connectedMac.equalsIgnoreCase(mac)) {
    selectedBmsMac = connectedMac;
    selectedBmsName = connectedName;
    saveSelectedBms();
    String ok = F("{\"type\":\"select_ok\",\"mac\":\"");
    ok += selectedBmsMac;
    ok += F("\"}");
    sendSetupJson(ok);
    sendSetupJson(buildStatusJson());
    Serial.printf("[SELECT] Already connected to requested BMS %s; NVS ensured\r\n", mac.c_str());
    return;
  }

  BmsCandidate candidate;
  const bool knownCandidate = findLastScanCandidate(mac, candidate);

  setAppState(SWITCHING_BMS, "mini program selected BMS");
  disconnectAndCleanup();
  scanForBms(mac);
  if (!bestCandidate.found || !bestCandidate.mac.equalsIgnoreCase(mac)) {
    NimBLEDevice::getScan()->clearResults();
    Serial.printf("[SELECT] Requested BMS %s not found; saved config unchanged\r\n", mac.c_str());
    enterConnectFailed("selected BMS not found", true);
    return;
  }

  if (!connectToBms()) {
    Serial.printf("[SELECT] Requested BMS %s connect failed; saved config unchanged\r\n", mac.c_str());
    enterConnectFailed("selected BMS connect failed", true);
    return;
  }

  selectedBmsMac = connectedMac;
  selectedBmsName = knownCandidate ? candidate.name : connectedName;
  if (selectedBmsName.length() == 0) {
    selectedBmsName = connectedName;
  }
  saveSelectedBms();

  String ok = F("{\"type\":\"select_ok\",\"mac\":\"");
  ok += selectedBmsMac;
  ok += F("\"}");
  sendSetupJson(ok);
  sendSetupJson(buildStatusJson());
}

static void handleForgetBmsCommand() {
  selectedBmsMac = "";
  selectedBmsName = "";
  preferences.remove(NVS_SELECTED_BMS_MAC);
  preferences.remove(NVS_SELECTED_BMS_NAME);
  Serial.println("[NVS] Cleared selected BMS");
  disconnectAndCleanup();
  enterWaitConfig("NO BMS SAVED", "WAIT CONFIG", "FORGET_BMS");
  sendSetupJson(buildStatusJson());
}

static void handleReconnectBmsCommand() {
  disconnectAndCleanup();
  connectSavedBms(true);
}

static void processPhoneCommand(String command) {
  command.trim();
  command.replace("\r", "");
  command.replace("\n", "");

  if (command.length() == 0) {
    return;
  }

  if (command == "PING") {
    sendSetupJson(F("{\"type\":\"pong\"}"));
  } else if (command == "GET_STATUS") {
    sendSetupJson(buildStatusJson());
  } else if (command == "SCAN_BMS") {
    handleManualScanCommand();
  } else if (command.startsWith("SELECT_BMS:")) {
    handleSelectBmsCommand(command.substring(strlen("SELECT_BMS:")));
  } else if (command == "FORGET_BMS") {
    handleForgetBmsCommand();
  } else if (command == "RECONNECT_BMS") {
    handleReconnectBmsCommand();
  } else if (command == "REBOOT") {
    sendSetupJson(F("{\"type\":\"rebooting\"}"));
    delay(300);
    ESP.restart();
  } else {
    sendSetupError("unknown command");
  }
}

static void processPhoneCommands() {
  if (phoneCommandQueue == nullptr) {
    return;
  }

  PhoneCommand command = {};
  while (xQueueReceive(phoneCommandQueue, &command, 0) == pdTRUE) {
    processPhoneCommand(String(command.text));
  }
}

static void initSetupBleService() {
  setupServer = NimBLEDevice::createServer();
  setupServer->setCallbacks(&setupServerCallbacks, false);

  NimBLEService *service = setupServer->createService(SETUP_SERVICE_UUID);
  NimBLECharacteristic *rxCharacteristic =
    service->createCharacteristic(SETUP_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  setupTxCharacteristic =
    service->createCharacteristic(SETUP_TX_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  rxCharacteristic->setCallbacks(&setupRxCallbacks);
  setupTxCharacteristic->setCallbacks(&setupTxCallbacks);
  setupTxCharacteristic->setValue("{}");

  setupServer->start();

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->setName(PHONE_ADV_NAME);
  advertising->addServiceUUID(service->getUUID());
  advertising->start();

  Serial.printf("[SETUP] Advertising as %s service=%s\r\n", PHONE_ADV_NAME, SETUP_SERVICE_UUID);
}

static void readBmsData() {
  if (!connected || !notifyReady) {
    return;
  }
  uint32_t now = millis();
  if (now - lastStatusRequestMs >= STATUS_REQUEST_INTERVAL_MS) {
    sendStatusRequest();
    lastStatusRequestMs = now;
  }
}

static void handleReconnect() {
  if (wasConnected && !connected) {
    wasConnected = false;
    disconnectAndCleanup();
    Serial.println("[CONNECT] Disconnected; waiting for mini program command");
    enterConnectFailed("BMS disconnected", false);
  }
}

static void initDisplay() {
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(170, 320);
  if (TFT_USE_CUSTOM_OFFSET) {
    tft.setPanelOffset(TFT_COL_OFFSET, TFT_ROW_OFFSET);
  }
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(TFT_INVERT);
  tft.setSPISpeed(40000000);
  u8g2Text.begin(tft);
  tft.fillScreen(C_BG());
  drawBootScreen();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  Serial.println();
  Serial.println("=== ANT BMS TFT Dashboard V2.2 State Machine ===");
  Serial.printf("[STATE] %s: setup start\r\n", appStateName(appState));
  Serial.printf("Serial baud: %u\r\n", (unsigned) SERIAL_BAUD);
  Serial.println("[DISPLAY] ST7789 170x320, landscape 320x170");
  Serial.printf("[DISPLAY] rotation=%u invert=%s customOffset=%s col=%d row=%d\r\n",
                TFT_ROTATION, TFT_INVERT ? "true" : "false",
                TFT_USE_CUSTOM_OFFSET ? "true" : "false",
                TFT_COL_OFFSET, TFT_ROW_OFFSET);

  initDisplay();

  preferences.begin("bms-dash", false);
  selectedBmsMac = normalizeMac(preferences.getString(NVS_SELECTED_BMS_MAC, ""));
  selectedBmsName = preferences.getString(NVS_SELECTED_BMS_NAME, "");
  Serial.printf("[NVS] selected_bms_mac=%s selected_bms_name=\"%s\"\r\n",
                selectedBmsMac.length() ? selectedBmsMac.c_str() : "(none)",
                selectedBmsName.c_str());

  phoneCommandQueue = xQueueCreate(4, sizeof(PhoneCommand));
  if (phoneCommandQueue == nullptr) {
    Serial.println("[SETUP] Failed to create command queue");
  }

  NimBLEDevice::init(PHONE_ADV_NAME);
  NimBLEDevice::setMTU(185);
  NimBLEDevice::setPower(9);
  initSetupBleService();
  setAppState(BLE_SETUP_READY, "setup BLE service ready");
  drawBleReadyScreen();

  if (selectedBmsMac.length() > 0) {
    connectSavedBms(false);
  } else {
    enterWaitConfig("NO BMS SAVED", "WAIT CONFIG", "boot without selected_bms_mac");
  }
}

void loop() {
  processPhoneCommands();
  handleReconnect();

  readBmsData();

  if (connected && notifyReady) {
    uint32_t now = millis();
    if (now - lastDashboardDrawMs >= DASHBOARD_REFRESH_MS) {
      drawMainDashboard(bmsData);
      lastDashboardDrawMs = now;
    }
  }

  delay(20);
}
