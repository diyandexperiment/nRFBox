/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/nrfbox
   ________________________________________ */
   
#ifndef setting_H
#define setting_H

#include <BLEDevice.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <RF24.h>
#include <vector>
#include <string>
#include <SD.h>
#include <Update.h>
#include <SPI.h>

#include <WiFi.h>        // String, WiFi ইত্যাদির জন্য
#include <WebServer.h>   // WebServer ক্লাসের জন্য

#include <time.h> 
bool getNtpTime(struct tm *timeinfo, uint32_t timeoutMs = 5000);

void neopixelSetup();
void neopixelLoop();

void setNeoPixelColour(const std::string& colour);
void flash(int numberOfFlashes, const std::vector<std::string>& colors, const std::string& finalColour);

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern Adafruit_NeoPixel pixels;

//extern bool neoPixelActive;

bool neoPixelActive = false;
uint8_t oledBrightness = 100;

extern RF24 RadioA;
extern RF24 RadioB;
extern RF24 RadioC;

void configureNrf(RF24 &radio);

void setRadiosNeutralState();

void setupRadioA();
void setupRadioB();
void setupRadioC();

void initAllRadios();

void Str(uint8_t x, uint8_t y, const uint8_t* asciiArray, size_t len);
void CenteredStr(uint8_t screenWidth, uint8_t y, const uint8_t* asciiArray, size_t len, const uint8_t* font);
void utils();
void conf();

namespace Setting {
  void settingSetup();
  void settingLoop();
  void startOTAMode();
  void stopOTAMode();
  void handleOTAUpload();
  void displayOTAInfo();
  void updateOTAProgress(int progress);
  String getOTAPage();
}

extern bool otaModeActive;
extern WebServer otaServer;
extern String otaSsid;
extern String otaPassword;
extern unsigned long otaStartTime;
extern int current_screen;

#endif
