#include <Arduino.h>
#include "tft_setup.h"
#include <TFT_eSPI.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>

#include "ui_font_zh.h"

// If the screen is mirrored, try 3 instead of 1.
static const uint8_t TFT_ROTATION = 1;
// If colors are inverted, change this to true/false.
static const bool TFT_INVERT = true;

static const uint16_t SCREEN_W = 320;
static const uint16_t SCREEN_H = 170;
static const uint32_t SERIAL_BAUD = 115200;
static const uint32_t BMS_SCAN_MS = 5000;
static const uint32_t STATUS_REQUEST_INTERVAL_MS = 1000;
static const size_t MAX_FRAME_SIZE = 192;
static const char *PHONE_ADV_NAME = "BMS-DASH-SETUP";
static const char *FIRMWARE_VERSION = "ANT_BMS_TFT_Dashboard_V2_4_1_DisplayFix";

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
  TRY_CONNECT_STRONGEST_BMS,
  BMS_CONNECTED,
  WAIT_CONFIG,
  SCANNING_BMS,
  SWITCHING_BMS,
  BMS_CONNECT_FAILED
};

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite frame = TFT_eSprite(&tft);
static TFT_eSprite region = TFT_eSprite(&tft);
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
static bool dashboardStaticDrawn = false;
static bool dashboardValuesDrawn = false;
static bool bmsDataDirty = false;
static bool uiTargetsReady = false;
static uint32_t lastDashboardAnimMs = 0;
static int dashboardCachedSocBar = -999;
static char dashboardCachedMosTemp[12] = "";
static char dashboardCachedSocText[8] = "";
static char dashboardCachedVoltage[12] = "";
static char dashboardCachedDelta[12] = "";
static char dashboardCachedCapacity[12] = "";
static AppState appState = BOOTING;
static uint32_t lastStatusRequestMs = 0;

struct DashboardUiValues {
  float voltage = NAN;
  float soc = NAN;
  float mosTemp = NAN;
  float deltaCellVoltageMv = NAN;
  float remainingCapacityAh = NAN;
  bool valid = false;
};

static DashboardUiValues uiShown;
static DashboardUiValues uiTarget;

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
static uint16_t C_ACCENT() { return rgb(70, 225, 207); }
static uint16_t C_ACCENT_SOFT() { return rgb(123, 215, 184); }

static const char *appStateName(AppState state) {
  switch (state) {
    case BOOTING: return "BOOTING";
    case BLE_SETUP_READY: return "BLE_SETUP_READY";
    case TRY_CONNECT_LAST_BMS: return "TRY_CONNECT_LAST_BMS";
    case TRY_CONNECT_STRONGEST_BMS: return "TRY_CONNECT_STRONGEST_BMS";
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

  dashboardStaticDrawn = false;
  dashboardValuesDrawn = false;
  Serial.printf("[STATE] %s -> %s", appStateName(appState), appStateName(nextState));
  if (reason != nullptr) {
    Serial.printf(": %s", reason);
  }
  Serial.println();
  appState = nextState;
}


static uint16_t uiDecodeUtf8(const char *&p) {
  const uint8_t c = (uint8_t) *p++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) {
    uint16_t cp = (c & 0x1F) << 6;
    cp |= ((uint8_t) *p++ & 0x3F);
    return cp;
  }
  if ((c & 0xF0) == 0xE0) {
    uint16_t cp = (c & 0x0F) << 12;
    cp |= (((uint8_t) *p++ & 0x3F) << 6);
    cp |= ((uint8_t) *p++ & 0x3F);
    return cp;
  }
  return '?';
}

static bool uiFindGlyph(uint16_t code, UiZhGlyph &glyph) {
  for (uint16_t i = 0; i < UI_ZH_GLYPH_COUNT; i++) {
    memcpy_P(&glyph, &UI_ZH_GLYPHS[i], sizeof(UiZhGlyph));
    if (glyph.code == code) {
      return true;
    }
  }
  return false;
}

static int16_t uiTextWidth(const char *text) {
  int16_t width = 0;
  const char *p = text;
  while (*p) {
    const uint16_t code = uiDecodeUtf8(p);
    if (code < 0x80) {
      width += code == ' ' ? 4 : 8;
      continue;
    }
    UiZhGlyph glyph;
    width += uiFindGlyph(code, glyph) ? glyph.advance : 9;
  }
  return width;
}

static void drawUiGlyph(TFT_eSprite &canvas, const UiZhGlyph &glyph, int16_t x, int16_t y, uint16_t color) {
  const uint8_t rowBytes = (glyph.width + 7) / 8;
  for (uint8_t yy = 0; yy < glyph.height; yy++) {
    for (uint8_t xb = 0; xb < rowBytes; xb++) {
      const uint8_t bits = pgm_read_byte(glyph.bitmap + yy * rowBytes + xb);
      for (uint8_t bit = 0; bit < 8; bit++) {
        const uint8_t xx = xb * 8 + bit;
        if (xx < glyph.width && (bits & (1 << bit))) {
          canvas.drawPixel(x + xx, y + yy, color);
        }
      }
    }
  }
}

static void drawUiText(TFT_eSprite &canvas, const char *text, int16_t x, int16_t y, uint16_t color) {
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(color);
  canvas.setTextSize(1);
  const char *p = text;
  int16_t cursor = x;
  while (*p) {
    const uint16_t code = uiDecodeUtf8(p);
    if (code < 0x80) {
      if (code == ' ') {
        cursor += 4;
      } else {
        char ascii[2] = { (char) code, '\0' };
        canvas.drawString(ascii, cursor, y, 2);
        cursor += canvas.textWidth(ascii, 2);
      }
      continue;
    }
    UiZhGlyph glyph;
    if (uiFindGlyph(code, glyph)) {
      drawUiGlyph(canvas, glyph, cursor, y, color);
      cursor += glyph.advance;
    } else {
      canvas.drawRect(cursor, y + 2, 7, 11, color);
      cursor += 9;
    }
  }
}

static void drawCenteredUiText(TFT_eSprite &canvas, const char *text, int16_t y, uint16_t color) {
  drawUiText(canvas, text, (SCREEN_W - uiTextWidth(text)) / 2, y, color);
}

static int16_t freeTextWidth(TFT_eSprite &canvas, const char *text, const GFXfont *font) {
  canvas.setFreeFont(font);
  return canvas.textWidth(text);
}

static void drawFreeText(TFT_eSprite &canvas, const char *text, int16_t x, int16_t y,
                         const GFXfont *font, uint16_t color) {
  canvas.setTextDatum(TL_DATUM);
  canvas.setFreeFont(font);
  canvas.setTextColor(color);
  canvas.drawString(text, x, y);
}

static void storeCachedText(char *cache, size_t cacheSize, const char *text) {
  strncpy(cache, text, cacheSize - 1);
  cache[cacheSize - 1] = '\0';
}

static void resetDashboardCache() {
  dashboardValuesDrawn = false;
  dashboardCachedSocBar = -999;
  dashboardCachedMosTemp[0] = '\0';
  dashboardCachedSocText[0] = '\0';
  dashboardCachedVoltage[0] = '\0';
  dashboardCachedDelta[0] = '\0';
  dashboardCachedCapacity[0] = '\0';
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



static const char UI_TXT_APP[] = "X\u4eea\u8868";
static const char UI_TXT_BOOTING[] = "\u542f\u52a8\u4e2d";
static const char UI_TXT_SELF_CHECK_DONE[] = "\u81ea\u68c0\u5b8c\u6210";
static const char UI_TXT_CONNECTING_BATTERY[] = "\u6b63\u5728\u8fde\u63a5\u7535\u6c60";
static const char UI_TXT_SEARCHING_BATTERY[] = "\u6b63\u5728\u641c\u7d22\u7535\u6c60";
static const char UI_TXT_PICK_ON_PHONE[] = "\u8bf7\u5728\u624b\u673a\u4e0a\u9009\u62e9\u4f60\u7684\u7535\u6c60";
static const char UI_TXT_CONNECT_LAST[] = "\u6b63\u5728\u5c1d\u8bd5\u8fde\u63a5\u4e0a\u6b21\u4f7f\u7528\u7684\u7535\u6c60";
static const char UI_TXT_CONNECT_NEARBY[] = "\u6b63\u5728\u8fde\u63a5\u9644\u8fd1\u7535\u6c60";
static const char UI_TXT_SWITCHING_BATTERY[] = "\u6b63\u5728\u5207\u6362\u7535\u6c60";
static const char UI_TXT_WAIT[] = "\u8bf7\u7a0d\u5019";
static const char UI_TXT_NOT_CONNECTED[] = "\u672a\u8fde\u63a5\u7535\u6c60";
static const char UI_TXT_FAILED[] = "\u8fde\u63a5\u5931\u8d25";
static const char UI_TXT_RECONNECTING[] = "\u6b63\u5728\u91cd\u65b0\u8fde\u63a5";
static const char UI_TXT_SOC_LABEL[] = "\u7535\u91cf";
static const char UI_TXT_NORMAL[] = "\u72b6\u6001\u6b63\u5e38";
static const char UI_TXT_NO_DATA[] = "\u6682\u65e0\u7535\u6c60\u6570\u636e";
static const char UI_TXT_VOLTAGE[] = "\u7535\u538b";
static const char UI_TXT_TEMP[] = "\u6e29\u5ea6";
static const char UI_TXT_DELTA[] = "\u538b\u5dee";
static const char UI_TXT_REMAIN[] = "\u5269\u4f59";

static void ensureFrame() {
  if (!frame.created()) {
    frame.setColorDepth(16);
    frame.createSprite(SCREEN_W, SCREEN_H);
  }
}

static bool beginRegion(int16_t w, int16_t h, uint16_t bg) {
  if (region.created()) {
    region.deleteSprite();
  }
  region.setColorDepth(16);
  if (region.createSprite(w, h) == nullptr) {
    return false;
  }
  region.fillSprite(bg);
  return true;
}

static void pushRegion(int16_t x, int16_t y) {
  region.pushSprite(x, y);
  region.deleteSprite();
}

static void drawAppTitle(TFT_eSprite &canvas, int16_t originX = 0, int16_t originY = 0) {
  drawUiText(canvas, UI_TXT_APP, 14 - originX, 9 - originY, C_TEXT());
  canvas.fillRoundRect(65 - originX, 14 - originY, 32, 2, 1, C_ACCENT());
}

static void drawBatteryLinkIcon(TFT_eSprite &canvas, int16_t x, int16_t y, uint16_t color) {
  canvas.drawRoundRect(x, y + 4, 18, 10, 3, color);
  canvas.fillRect(x + 18, y + 7, 3, 4, color);
  canvas.drawLine(x + 25, y + 9, x + 34, y + 9, color);
  canvas.fillCircle(x + 38, y + 9, 2, color);
}

static void drawPhoneIcon(TFT_eSprite &canvas, int16_t x, int16_t y, uint16_t color) {
  canvas.drawRoundRect(x, y, 12, 20, 3, color);
  canvas.drawFastHLine(x + 4, y + 3, 4, color);
  canvas.fillCircle(x + 6, y + 16, 1, color);
}

static void drawConnectionIcons(TFT_eSprite &canvas, bool bmsOk, bool phoneOk,
                                int16_t originX = 0, int16_t originY = 0) {
  int16_t x = 276 - originX;
  const int16_t y = 9 - originY;
  if (bmsOk) {
    drawBatteryLinkIcon(canvas, x, y, C_ACCENT());
    x -= 22;
  }
  if (phoneOk) {
    drawPhoneIcon(canvas, x, y - 1, C_TEXT());
  }
}

static void drawThickArc(TFT_eSprite &canvas, int16_t cx, int16_t cy, int16_t radius,
                         int16_t dotRadius, int16_t startDeg, int16_t endDeg, uint16_t color) {
  if (endDeg < startDeg) {
    endDeg += 360;
  }
  for (int16_t angle = startDeg; angle <= endDeg; angle += 3) {
    const float rad = (angle - 90) * 0.0174532925f;
    const int16_t x = cx + int16_t(cosf(rad) * radius);
    const int16_t y = cy + int16_t(sinf(rad) * radius);
    canvas.fillCircle(x, y, dotRadius, color);
  }
}

static void drawBootMotorcycle(TFT_eSprite &canvas, uint8_t frameNo, uint8_t totalFrames) {
  const float progress = float(frameNo + 1) / float(totalFrames);
  const uint16_t lineColor = progress > 0.45f ? C_ACCENT() : C_DIM();
  const int16_t sweep = int16_t(progress * SCREEN_W);

  canvas.fillSprite(C_BG());
  drawAppTitle(canvas);
  canvas.drawFastHLine(24, 138, sweep > 272 ? 272 : sweep, rgb(43, 77, 84));

  const int16_t y = 94;
  const int16_t w1x = 102;
  const int16_t w2x = 216;
  const int16_t r = 24;
  canvas.drawCircle(w1x, y, r, rgb(50, 66, 70));
  canvas.drawCircle(w2x, y, r, rgb(50, 66, 70));
  drawThickArc(canvas, w1x, y, r, 2, frameNo * 8, frameNo * 8 + 110, lineColor);
  drawThickArc(canvas, w2x, y, r, 2, 180 + frameNo * 8, 290 + frameNo * 8, lineColor);
  canvas.drawLine(w1x + 8, y - 9, 144, y - 38, lineColor);
  canvas.drawLine(144, y - 38, 184, y - 39, lineColor);
  canvas.drawLine(184, y - 39, w2x - 10, y - 10, lineColor);
  canvas.drawLine(130, y - 4, 184, y - 4, rgb(115, 156, 158));
  canvas.drawLine(164, y - 42, 174, y - 57, rgb(115, 156, 158));
  canvas.drawLine(174, y - 57, 190, y - 54, rgb(115, 156, 158));

  drawCenteredUiText(canvas, UI_TXT_BOOTING, 31, C_MUTED());
  if (progress > 0.68f) {
    drawCenteredUiText(canvas, UI_TXT_SELF_CHECK_DONE, 145, C_TEXT());
  }
}

static void drawBootAnimation() {
  ensureFrame();
  const uint8_t totalFrames = 60;
  for (uint8_t i = 0; i < totalFrames; i++) {
    drawBootMotorcycle(frame, i, totalFrames);
    frame.pushSprite(0, 0);
    delay(32);
  }
}

static void drawBatteryPulse(TFT_eSprite &canvas, int16_t cx, int16_t cy, uint16_t accent) {
  const uint8_t phase = (millis() / 180) % 16;
  canvas.drawRoundRect(cx - 36, cy - 18, 62, 36, 8, rgb(51, 70, 76));
  canvas.fillRoundRect(cx + 28, cy - 7, 8, 14, 3, rgb(51, 70, 76));
  const int16_t fillW = 12 + (phase < 8 ? phase : 15 - phase) * 5;
  canvas.fillRoundRect(cx - 31, cy - 13, fillW, 26, 6, accent);
  for (int i = 0; i < 3; i++) {
    const bool on = ((millis() / 280) + i) % 3 == 0;
    canvas.fillCircle(cx - 12 + i * 12, cy + 32, 3, on ? accent : C_DIM());
  }
}

static const char *connectingSubtitle() {
  if (appState == TRY_CONNECT_LAST_BMS) {
    return UI_TXT_CONNECT_LAST;
  }
  if (appState == TRY_CONNECT_STRONGEST_BMS) {
    return UI_TXT_CONNECT_NEARBY;
  }
  if (appState == SCANNING_BMS) {
    return UI_TXT_PICK_ON_PHONE;
  }
  if (appState == SWITCHING_BMS) {
    return UI_TXT_WAIT;
  }
  return UI_TXT_CONNECT_NEARBY;
}

static void drawStateScreen(const char *title, const char *subtitle, uint16_t accent, bool showPulse) {
  ensureFrame();
  frame.fillSprite(C_BG());
  frame.fillRoundRect(10, 32, 300, 124, 12, C_PANEL());
  frame.drawRoundRect(10, 32, 300, 124, 12, rgb(22, 39, 45));
  drawAppTitle(frame);
  drawConnectionIcons(frame, connected, phoneConnected);
  if (showPulse) {
    drawBatteryPulse(frame, 160, 82, accent);
  }
  drawCenteredUiText(frame, title, 111, C_TEXT());
  drawCenteredUiText(frame, subtitle, 134, C_MUTED());
  frame.pushSprite(0, 0);
  screenReady = true;
}

static void drawBootScreen() {
  drawBootAnimation();
}

static void drawBleReadyScreen() {
  drawStateScreen(UI_TXT_CONNECTING_BATTERY, UI_TXT_WAIT, C_ACCENT(), true);
}

static void drawWaitConfigScreen(const char *reason) {
  (void) reason;
  drawStateScreen(UI_TXT_NOT_CONNECTED, UI_TXT_PICK_ON_PHONE, C_ACCENT_SOFT(), false);
}

static void drawTryLastBmsScreen() {
  drawStateScreen(UI_TXT_CONNECTING_BATTERY, UI_TXT_CONNECT_LAST, C_ACCENT(), true);
}

static void drawTryStrongestBmsScreen() {
  drawStateScreen(UI_TXT_CONNECTING_BATTERY, UI_TXT_CONNECT_NEARBY, C_ACCENT(), true);
}

static void drawManualBmsScanScreen() {
  drawStateScreen(UI_TXT_SEARCHING_BATTERY, UI_TXT_PICK_ON_PHONE, C_ACCENT(), true);
}

static void drawSwitchingBmsScreen(const String &mac) {
  (void) mac;
  drawStateScreen(UI_TXT_SWITCHING_BATTERY, UI_TXT_WAIT, C_ACCENT(), true);
}

static void drawConnectFailedScreen(const char *reason) {
  (void) reason;
  drawStateScreen(UI_TXT_FAILED, UI_TXT_RECONNECTING, C_ACCENT_SOFT(), false);
}

static void drawScanScreen() {
  if (appState == TRY_CONNECT_LAST_BMS) {
    drawTryLastBmsScreen();
  } else if (appState == TRY_CONNECT_STRONGEST_BMS) {
    drawTryStrongestBmsScreen();
  } else if (appState == SWITCHING_BMS) {
    drawSwitchingBmsScreen(preferredScanMac);
  } else {
    drawManualBmsScanScreen();
  }
}

static void drawConnectingScreen(const String &deviceName, int rssi) {
  (void) deviceName;
  (void) rssi;
  drawStateScreen(UI_TXT_CONNECTING_BATTERY, connectingSubtitle(), C_ACCENT(), true);
}

static void drawMessageScreen(const char *line1, const char *line2) {
  drawStateScreen(line1, line2, C_ACCENT_SOFT(), false);
}

static void formatFloatText(char *out, size_t outSize, float value, uint8_t decimals) {
  if (isnan(value)) {
    snprintf(out, outSize, "--");
    return;
  }
  char fmt[8];
  snprintf(fmt, sizeof(fmt), "%%.%uf", decimals);
  snprintf(out, outSize, fmt, value);
}

static void formatIntText(char *out, size_t outSize, float value) {
  if (isnan(value)) {
    snprintf(out, outSize, "--");
  } else {
    snprintf(out, outSize, "%d", int(roundf(value)));
  }
}

static void drawNumberUnitRight(TFT_eSprite &canvas, const char *number, const char *unit,
                                int16_t rightX, int16_t y, uint16_t numberColor, uint16_t unitColor) {
  const int16_t numW = freeTextWidth(canvas, number, &FreeSansBold9pt7b);
  const int16_t unitW = unit[0] ? freeTextWidth(canvas, unit, &FreeSans9pt7b) : 0;
  const int16_t gap = unit[0] ? 3 : 0;
  const int16_t x = rightX - numW - unitW - gap;
  drawFreeText(canvas, number, x, y, &FreeSansBold9pt7b, numberColor);
  if (unit[0]) {
    if (strcmp(unit, "C") == 0) {
      canvas.drawCircle(x + numW + gap + 3, y + 5, 2, unitColor);
      drawFreeText(canvas, "C", x + numW + gap + 8, y, &FreeSans9pt7b, unitColor);
    } else {
      drawFreeText(canvas, unit, x + numW + gap, y, &FreeSans9pt7b, unitColor);
    }
  }
}

static void drawMetricRow(TFT_eSprite &canvas, int16_t originX, int16_t originY,
                          int16_t rowY, const char *label, const char *number, const char *unit) {
  const int16_t x = 186 - originX;
  const int16_t y = rowY - originY;
  canvas.fillRoundRect(x, y, 124, 27, 7, C_PANEL_2());
  canvas.drawRoundRect(x, y, 124, 27, 7, rgb(27, 47, 53));
  drawUiText(canvas, label, x + 10, y + 6, C_MUTED());
  drawNumberUnitRight(canvas, number, unit, x + 114, y + 6, C_TEXT(), C_MUTED());
}

static void drawSocPanel(TFT_eSprite &canvas, int16_t originX, int16_t originY, const DashboardUiValues &values) {
  const int16_t x = 10 - originX;
  const int16_t y = 32 - originY;
  const int16_t cx = 94 - originX;
  const int16_t cy = 92 - originY;
  canvas.fillRoundRect(x, y, 166, 124, 12, C_PANEL());
  canvas.drawRoundRect(x, y, 166, 124, 12, rgb(22, 39, 45));

  const int soc = isnan(values.soc) ? -1 : constrain(int(roundf(values.soc)), 0, 100);
  drawThickArc(canvas, cx, cy, 46, 2, 0, 360, rgb(31, 49, 55));
  if (soc >= 0) {
    drawThickArc(canvas, cx, cy, 46, 3, 0, int(soc * 3.6f), getSocColor(soc));
  }

  char socText[8];
  formatIntText(socText, sizeof(socText), values.soc);
  canvas.setTextDatum(TL_DATUM);
  const int16_t numW = freeTextWidth(canvas, socText, &FreeSansBold24pt7b);
  const int16_t pctW = freeTextWidth(canvas, "%", &FreeSansBold12pt7b);
  const int16_t totalW = numW + pctW + 2;
  const int16_t textX = cx - totalW / 2;
  drawFreeText(canvas, socText, textX, cy - 27, &FreeSansBold24pt7b, C_TEXT());
  drawFreeText(canvas, "%", textX + numW + 2, cy - 13, &FreeSansBold12pt7b, C_MUTED());
  drawUiText(canvas, UI_TXT_SOC_LABEL, cx - uiTextWidth(UI_TXT_SOC_LABEL) / 2, cy + 34, C_MUTED());
  drawUiText(canvas, values.valid ? UI_TXT_NORMAL : UI_TXT_NO_DATA, 31 - originX, 139 - originY,
             values.valid ? C_ACCENT_SOFT() : C_MUTED());
}

static void drawMetricPanel(TFT_eSprite &canvas, int16_t originX, int16_t originY, const DashboardUiValues &values) {
  char voltage[12];
  char temp[12];
  char delta[12];
  char capacity[12];
  formatFloatText(voltage, sizeof(voltage), values.voltage, 1);
  formatIntText(temp, sizeof(temp), values.mosTemp);
  formatIntText(delta, sizeof(delta), values.deltaCellVoltageMv);
  formatFloatText(capacity, sizeof(capacity), values.remainingCapacityAh, 1);
  drawMetricRow(canvas, originX, originY, 38, UI_TXT_VOLTAGE, voltage, isnan(values.voltage) ? "" : "V");
  drawMetricRow(canvas, originX, originY, 69, UI_TXT_TEMP, temp, isnan(values.mosTemp) ? "" : "C");
  drawMetricRow(canvas, originX, originY, 100, UI_TXT_DELTA, delta, isnan(values.deltaCellVoltageMv) ? "" : "mV");
  drawMetricRow(canvas, originX, originY, 131, UI_TXT_REMAIN, capacity, isnan(values.remainingCapacityAh) ? "" : "Ah");
}

static void drawDashboardFrame(TFT_eSprite &canvas, const DashboardUiValues &values) {
  canvas.fillSprite(C_BG());
  drawAppTitle(canvas);
  drawConnectionIcons(canvas, true, phoneConnected);
  drawSocPanel(canvas, 0, 0, values);
  drawMetricPanel(canvas, 0, 0, values);
}

static void drawDashboardStatic() {
  ensureFrame();
  DashboardUiValues values = uiTargetsReady ? uiShown : DashboardUiValues();
  drawDashboardFrame(frame, values);
  frame.pushSprite(0, 0);
  dashboardStaticDrawn = true;
  resetDashboardCache();
}

static bool approachValue(float &current, float target, float alpha, float snap) {
  if (isnan(target)) {
    if (!isnan(current)) {
      current = NAN;
      return true;
    }
    return false;
  }
  if (isnan(current)) {
    current = target;
    return true;
  }
  const float diff = target - current;
  if (fabsf(diff) <= snap) {
    if (current != target) {
      current = target;
      return true;
    }
    return false;
  }
  current += diff * alpha;
  return true;
}

static void setDashboardTarget(const BmsData &data) {
  uiTarget.valid = data.valid;
  uiTarget.voltage = data.valid && !isnan(data.voltage) ? data.voltage : NAN;
  uiTarget.soc = data.valid && data.soc >= 0 ? float(data.soc) : NAN;
  uiTarget.mosTemp = data.valid && !isnan(data.mosTemp) ? data.mosTemp : NAN;
  uiTarget.deltaCellVoltageMv = data.valid && data.deltaCellVoltageMv >= 0 ? float(data.deltaCellVoltageMv) : NAN;
  uiTarget.remainingCapacityAh = data.valid && !isnan(data.remainingCapacityAh) ? data.remainingCapacityAh : NAN;
  if (!uiTargetsReady) {
    uiShown = uiTarget;
    uiTargetsReady = true;
    resetDashboardCache();
  }
}

static void redrawDashboardRegions(bool force) {
  char socText[8];
  char voltage[12];
  char temp[12];
  char delta[12];
  char capacity[12];
  formatIntText(socText, sizeof(socText), uiShown.soc);
  formatFloatText(voltage, sizeof(voltage), uiShown.voltage, 1);
  formatIntText(temp, sizeof(temp), uiShown.mosTemp);
  formatIntText(delta, sizeof(delta), uiShown.deltaCellVoltageMv);
  formatFloatText(capacity, sizeof(capacity), uiShown.remainingCapacityAh, 1);

  const int socBar = isnan(uiShown.soc) ? -1 : constrain(int(roundf(uiShown.soc)), 0, 100);
  const bool socChanged = force || dashboardCachedSocBar != socBar || strcmp(dashboardCachedSocText, socText) != 0;
  const bool metricsChanged = force || strcmp(dashboardCachedVoltage, voltage) != 0 ||
                              strcmp(dashboardCachedMosTemp, temp) != 0 ||
                              strcmp(dashboardCachedDelta, delta) != 0 ||
                              strcmp(dashboardCachedCapacity, capacity) != 0;

  if (socChanged && beginRegion(170, 130, C_BG())) {
    drawSocPanel(region, 6, 30, uiShown);
    pushRegion(6, 30);
    dashboardCachedSocBar = socBar;
    storeCachedText(dashboardCachedSocText, sizeof(dashboardCachedSocText), socText);
  }
  if (metricsChanged && beginRegion(134, 126, C_BG())) {
    drawMetricPanel(region, 180, 36, uiShown);
    pushRegion(180, 36);
    storeCachedText(dashboardCachedVoltage, sizeof(dashboardCachedVoltage), voltage);
    storeCachedText(dashboardCachedMosTemp, sizeof(dashboardCachedMosTemp), temp);
    storeCachedText(dashboardCachedDelta, sizeof(dashboardCachedDelta), delta);
    storeCachedText(dashboardCachedCapacity, sizeof(dashboardCachedCapacity), capacity);
  }
  dashboardValuesDrawn = true;
}

static void updateDashboardValues(bool force = false) {
  if (!uiTargetsReady) {
    return;
  }
  if (!dashboardStaticDrawn) {
    drawDashboardStatic();
    force = true;
  }
  const uint32_t now = millis();
  if (!force && now - lastDashboardAnimMs < 33) {
    return;
  }
  lastDashboardAnimMs = now;
  bool changed = force;
  changed |= approachValue(uiShown.soc, uiTarget.soc, 0.22f, 0.2f);
  changed |= approachValue(uiShown.voltage, uiTarget.voltage, 0.18f, 0.04f);
  changed |= approachValue(uiShown.mosTemp, uiTarget.mosTemp, 0.18f, 0.2f);
  changed |= approachValue(uiShown.deltaCellVoltageMv, uiTarget.deltaCellVoltageMv, 0.24f, 0.6f);
  changed |= approachValue(uiShown.remainingCapacityAh, uiTarget.remainingCapacityAh, 0.18f, 0.04f);
  uiShown.valid = uiTarget.valid;
  if (changed) {
    redrawDashboardRegions(force);
  }
}

static void drawDashboardValues(const BmsData &data) {
  setDashboardTarget(data);
  updateDashboardValues(true);
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
  bmsDataDirty = true;

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
    dashboardStaticDrawn = false;
    dashboardValuesDrawn = false;
    bmsDataDirty = false;
    bmsCharacteristic = nullptr;
    frameBuffer.clear();
    Serial.printf("[CONNECT] BLE client disconnected reason=%d\r\n", reason);
  }
} bmsClientCallbacks;

class SetupServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
    phoneConnected = true;
    dashboardStaticDrawn = false;
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
    dashboardStaticDrawn = false;
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
  dashboardStaticDrawn = false;
  dashboardValuesDrawn = false;
  bmsDataDirty = false;
  uiTargetsReady = false;
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
  setAppState(BMS_CONNECTED, "BMS connection ready");
  drawDashboardStatic();
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
    Serial.printf("[POLICY] Saved BMS %s not found; boot fallback may try strongest nearby BMS\r\n", selectedBmsMac.c_str());
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

static bool connectStrongestBmsAtBoot(bool reportToPhone) {
  setAppState(TRY_CONNECT_STRONGEST_BMS, "boot fallback scans strongest BMS");
  BmsCandidate candidate = scanForBms("");

  if (!candidate.found) {
    NimBLEDevice::getScan()->clearResults();
    Serial.println("[POLICY] No ANT/BMS candidate found; waiting for mini program config");
    enterWaitConfig("NO BMS FOUND", "WAIT CONFIG", "auto strongest BMS not found");
    if (reportToPhone) {
      sendSetupError("no_bms_found");
    }
    return false;
  }

  Serial.printf("[POLICY] Boot fallback selected strongest BMS: name=\"%s\" mac=%s rssi=%d\r\n",
                candidate.name.c_str(), candidate.mac.c_str(), candidate.rssi);

  if (!connectToBms()) {
    NimBLEDevice::getScan()->clearResults();
    Serial.printf("[POLICY] Strongest BMS %s connect failed\r\n", candidate.mac.c_str());
    enterConnectFailed("auto strongest connect failed", reportToPhone);
    return false;
  }

  selectedBmsMac = connectedMac;
  selectedBmsName = connectedName;
  saveSelectedBms();
  Serial.printf("[POLICY] Strongest BMS saved as current boot target: %s\r\n",
                selectedBmsMac.c_str());

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
    drawDashboardStatic();
    bmsDataDirty = bmsData.valid;
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
  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(TFT_INVERT);
  frame.setColorDepth(16);
  frame.createSprite(SCREEN_W, SCREEN_H);
  tft.fillScreen(C_BG());
  drawBootScreen();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  Serial.println();
  Serial.printf("=== ANT BMS TFT Dashboard %s Customer UI ===\r\n", FIRMWARE_VERSION);
  Serial.printf("[STATE] %s: setup start\r\n", appStateName(appState));
  Serial.printf("Serial baud: %u\r\n", (unsigned) SERIAL_BAUD);
  Serial.println("[DISPLAY] TFT_eSPI ST7789 170x320, landscape 320x170");
  Serial.printf("[DISPLAY] rotation=%u invert=%s spi=40000000\r\n",
                TFT_ROTATION, TFT_INVERT ? "true" : "false");

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

  Serial.println("[POLICY] Boot tries saved BMS first, then falls back to strongest nearby BMS");
  bool bootConnected = false;
  if (selectedBmsMac.length() > 0) {
    bootConnected = connectSavedBms(false);
  }
  if (!bootConnected) {
    connectStrongestBmsAtBoot(false);
  }
}

void loop() {
  processPhoneCommands();
  handleReconnect();

  readBmsData();

  if (connected && notifyReady) {
    if (bmsDataDirty) {
      drawDashboardValues(bmsData);
      bmsDataDirty = false;
    } else {
      updateDashboardValues(false);
    }
  }

  delay(20);
}
