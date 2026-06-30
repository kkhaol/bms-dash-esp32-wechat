#ifndef BMS_DASH_TFT_SETUP_H
#define BMS_DASH_TFT_SETUP_H

#define USER_SETUP_LOADED
#define USER_SETUP_INFO "ANT_BMS_TFT_Dashboard_V3_5_FlickerFix"

#define ST7789_DRIVER
#define TFT_WIDTH 170
#define TFT_HEIGHT 320

#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS 5
#define TFT_DC 16
#define TFT_RST 17
#define TFT_MISO -1

#define TFT_RGB_ORDER TFT_BGR
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#endif
