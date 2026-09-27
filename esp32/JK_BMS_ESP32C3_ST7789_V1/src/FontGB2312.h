#ifndef FONT_GB2312_H
#define FONT_GB2312_H

#include <Arduino.h>
#include <TFT_eSPI.h>

/*
 * 16x16 中文点阵显示。
 *
 * 说明：
 * 1. 文件名沿用 FontGB2312，实际输入使用 Arduino/GCC UTF-8 字符串。
 * 2. 内置当前 JK BMS 屏幕所需的全部中文字符，避免 TFT_eSPI 默认字体无法显示中文。
 * 3. 支持中文 + ASCII 混排，支持 1 倍和 2 倍显示。
 * 4. 不改变 TFT_eSPI 原有数字字体，电压/电流/SOC 等数值仍使用 TFT_eSPI。
 */
class FontGB2312 {
public:
  static void drawText(TFT_eSprite& sprite, int16_t x, int16_t y,
                       const String& text, uint16_t color,
                       uint16_t bg = TFT_BLACK, uint8_t scale = 1);

  static void drawCenterString(TFT_eSprite& sprite, int16_t centerX,
                               int16_t y, const String& text,
                               uint16_t color, uint16_t bg = TFT_BLACK,
                               uint8_t scale = 1);

  static void drawRightString(TFT_eSprite& sprite, int16_t rightX,
                              int16_t y, const String& text,
                              uint16_t color, uint16_t bg = TFT_BLACK,
                              uint8_t scale = 1);

  static int16_t textWidth(const String& text, uint8_t scale = 1);
};

#endif
