/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/nrfbox
   ________________________________________ */

#include "setting.h"
#include "icon.h"
#include "config.h"

RF24 RadioA(NRF_CE_PIN_A, NRF_CSN_PIN_A);
RF24 RadioB(NRF_CE_PIN_B, NRF_CSN_PIN_B);
RF24 RadioC(NRF_CE_PIN_C, NRF_CSN_PIN_C);

void setRadiosNeutralState() {
  RadioA.stopListening();
  RadioA.setAutoAck(false);
  RadioA.setRetries(0, 0);
  RadioA.powerDown(); 
  digitalWrite(NRF_CE_PIN_A, LOW); 

  RadioB.stopListening();
  RadioB.setAutoAck(false);
  RadioB.setRetries(0, 0);
  RadioB.powerDown(); 
  digitalWrite(NRF_CE_PIN_B, LOW); 

  RadioC.stopListening();
  RadioC.setAutoAck(false);
  RadioC.setRetries(0, 0);
  RadioC.powerDown(); 
  digitalWrite(NRF_CE_PIN_C, LOW); 
}

void configureNrf(RF24 &radio) {
  radio.begin();
  radio.setAutoAck(false);
  radio.stopListening();
  radio.setRetries(0, 0);
  radio.setPALevel(RF24_PA_MAX, true);
  radio.setDataRate(RF24_2MBPS);
  radio.setCRCLength(RF24_CRC_DISABLED);
}

void setupRadioA() {
  configureNrf(RadioA);
}

void setupRadioB() {
  configureNrf(RadioB);
}

void setupRadioC() {
  configureNrf(RadioC);
}

void initAllRadios() {
  setupRadioA();
  setupRadioB();
  setupRadioC();
}

void Str(uint8_t x, uint8_t y, const uint8_t* asciiArray, size_t len) {
  char buf[64]; 
  for (size_t i = 0; i < len && i < sizeof(buf) - 1; i++) {
    buf[i] = (char)asciiArray[i];
  }
  buf[len] = '\0';

  u8g2.drawStr(x, y, buf);
}

void CenteredStr(uint8_t screenWidth, uint8_t y, const uint8_t* asciiArray, size_t len, const uint8_t* font) {
  char buf[64];
  for (size_t i = 0; i < len && i < sizeof(buf) - 1; i++) {
    buf[i] = (char)asciiArray[i];
  }
  buf[len] = '\0';

  u8g2.setFont((const uint8_t*)font);
  int16_t w = u8g2.getUTF8Width(buf);
  u8g2.setCursor((screenWidth - w) / 2, y);
  u8g2.print(buf);
}

void utils() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  Str(7, 15, Line_A, sizeof(Line_A));
  Str(12, 35, Line_B, sizeof(Line_B));
  Str(7, 55, Line_C, sizeof(Line_C));
  u8g2.sendBuffer();
}

void conf() {
  u8g2.setBitmapMode(1);
  u8g2.clearBuffer();
  CenteredStr(128, 25, txt_n, sizeof(txt_n), u8g2_font_ncenB14_tr);
  CenteredStr(106, 40, txt_c, sizeof(txt_c), u8g2_font_ncenB08_tr);
  CenteredStr(128, 60, txt_v, sizeof(txt_v), u8g2_font_6x10_tf);
  u8g2.sendBuffer();
  delay(3000);
  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 0, 128, 64, cred); 
  u8g2.sendBuffer();
  delay(250);
}

namespace Setting {

#define EEPROM_ADDRESS_NEOPIXEL 0
#define EEPROM_ADDRESS_BRIGHTNESS 1

int currentOption = 0;
int totalOptions = 3; 

bool buttonUpPressed = false;
bool buttonDownPressed = false;
bool buttonSelectPressed = false;

void updateFirmware() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 15, "Updating Firmware.");
  u8g2.sendBuffer();

  u8g2.setFont(u8g2_font_5x8_tr);
  if (!SD.begin(SD_CS_PIN)) {
    u8g2.drawStr(0, 30, "[ SD Init Failed ]");
    u8g2.sendBuffer();
    delay(2000);
    return;
  }

  if (!SD.exists(FIRMWARE_FILE)) {
    u8g2.drawStr(0, 30, "[ File Not Found ]");
    u8g2.sendBuffer();
    delay(2000);
    return;
  }

  File firmware = SD.open(FIRMWARE_FILE);
  if (!firmware) {
    u8g2.drawStr(0, 30, "[ Open Failed ]");
    u8g2.sendBuffer();
    delay(2000);
    return;
  }

  if (firmware) {
    u8g2.drawStr(0, 30, "[ Wait a Moment ]");
    u8g2.sendBuffer();
  }

  if (!Update.begin(firmware.size())) {
    u8g2.drawStr(0, 30, "[ Update Init Failed ]");
    u8g2.sendBuffer();
    firmware.close();
    delay(2000);
    return;
  }

  Update.writeStream(firmware);
  if (Update.end(true)) {
    u8g2.drawStr(0, 45, "[ Update Success! ]");
    u8g2.sendBuffer();
    delay(1000);
    ESP.restart();
  } else {
    u8g2.drawStr(0, 45, "[ Update Failed ]");
    u8g2.sendBuffer();
    delay(2000);
  }

  firmware.close();
}

// ========== OTA মোড চালু করার ফাংশন ==========
void startOTAMode() {
  Serial.println("Starting OTA Mode...");
  
  // ১. Wi-Fi AP মোড শুরু করুন
  WiFi.mode(WIFI_AP);
  WiFi.softAP(otaSsid.c_str(), otaPassword.c_str());
  
  // ২. OLED তে ওয়াইফাই তথ্য দেখান
  displayOTAInfo();
  
  // ৩. OTA সক্রিয় করুন
  otaModeActive = true;
  otaStartTime = millis();
  
  // ৪. ওয়েব সার্ভারের রুট সেট করুন
  otaServer.on("/update", HTTP_POST, []() {
    // আপলোড শেষ হলে রেসপন্স
    otaServer.send(200, "text/plain", "Update Success! Rebooting...");
    delay(1000);
    ESP.restart();
  }, []() {
    // ফাইল আপলোড হ্যান্ডলার
    handleOTAUpload();
  });
  
  otaServer.on("/", HTTP_GET, []() {
    // হোম পেজ (আপডেট পেজ)
    String html = getOTAPage();
    otaServer.send(200, "text/html", html);
  });
  
  otaServer.begin();
  Serial.println("OTA Server started at 192.168.4.1");
}

// ========== ফাইল আপলোড হ্যান্ডলার ==========
void handleOTAUpload() {
  HTTPUpload& upload = otaServer.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Update: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // প্রোগ্রেস বার আপডেট (আমরা OLED আপডেট করব)
    static int lastProgress = -1;
    int progress = (upload.totalSize > 0) ? (upload.currentSize * 100 / upload.totalSize) : 0;
    if (progress != lastProgress) {
      lastProgress = progress;
      updateOTAProgress(progress);   // OLED তে প্রোগ্রেস দেখান
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

// ========== OLED তে OTA তথ্য দেখানো ==========
void displayOTAInfo() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "OTA Mode Active");
  u8g2.drawStr(0, 25, ("SSID: " + otaSsid).c_str());
  u8g2.drawStr(0, 40, ("Pass: " + otaPassword).c_str());
  u8g2.drawStr(0, 55, "IP: 192.168.4.1");
  u8g2.sendBuffer();
}

// ========== OLED তে প্রোগ্রেস বার দেখানো ==========
void updateOTAProgress(int progress) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "OTA Uploading...");
  
  // প্রোগ্রেস বার আঁকুন (আয়তক্ষেত্র)
  u8g2.drawFrame(5, 25, 118, 20);            // বর্ডার
  u8g2.drawBox(7, 27, (progress * 114) / 100, 16);  // ফিল (প্রোগ্রেস অনুযায়ী)
  
  // পার্সেন্টেজ দেখান
  u8g2.setCursor(50, 40);
  u8g2.print(progress);
  u8g2.print("%");
  
  u8g2.sendBuffer();
}

// ========== HTML পেজ তৈরি ==========
String getOTAPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>nRFBox OTA Update</title>
  <style>
    body { font-family: Arial; background: #1a1a2e; color: #fff; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
    .container { background: #16213e; padding: 40px; border-radius: 20px; box-shadow: 0 10px 30px rgba(0,0,0,0.5); text-align: center; max-width: 400px; width: 100%; }
    h1 { color: #e94560; margin-bottom: 20px; }
    input[type="file"] { display: none; }
    .custom-file-upload { background: #e94560; color: #fff; padding: 12px 25px; border-radius: 30px; cursor: pointer; display: inline-block; transition: 0.3s; }
    .custom-file-upload:hover { background: #c73652; transform: scale(1.05); }
    #file-name { margin: 15px 0; color: #aaa; }
    button { background: #0f3460; color: #fff; border: none; padding: 12px 30px; border-radius: 30px; font-size: 16px; cursor: pointer; transition: 0.3s; }
    button:hover { background: #1a4a7a; transform: scale(1.05); }
    #progress { display: none; margin-top: 20px; }
    #progress-bar { width: 100%; height: 20px; background: #333; border-radius: 10px; overflow: hidden; }
    #progress-fill { height: 100%; width: 0%; background: #e94560; transition: width 0.5s; }
    #progress-text { margin-top: 10px; color: #aaa; }
  </style>
</head>
<body>
  <div class="container">
    <h1>⚡ nRFBox OTA</h1>
    <form id="update-form" method="POST" action="/update" enctype="multipart/form-data">
      <label for="file-input" class="custom-file-upload">Choose Firmware</label>
      <input type="file" id="file-input" name="firmware" accept=".bin">
      <div id="file-name">No file selected</div>
      <button type="submit">Update</button>
    </form>
    <div id="progress">
      <div id="progress-bar"><div id="progress-fill"></div></div>
      <div id="progress-text">0%</div>
    </div>
  </div>
  <script>
    document.getElementById('file-input').addEventListener('change', function(e) {
      var fileName = e.target.files[0] ? e.target.files[0].name : 'No file selected';
      document.getElementById('file-name').textContent = fileName;
    });

    document.getElementById('update-form').addEventListener('submit', function(e) {
      // প্রোগ্রেস দেখান
      document.getElementById('progress').style.display = 'block';
    });
  </script>
</body>
</html>
)rawliteral";
  return html;
}

// ========== OTA স্টপ ফাংশন (যদি প্রয়োজন) ==========
void stopOTAMode() {
  otaModeActive = false;
  otaServer.stop();
  WiFi.mode(WIFI_OFF);
  Serial.println("OTA Mode stopped.");
  // OLED তে মেইন মেনু দেখান (যদি প্রয়োজন)
}


void toggleOption(int option) {
  if (option == 0) { 
    neoPixelActive = !neoPixelActive;
    EEPROM.write(EEPROM_ADDRESS_NEOPIXEL, neoPixelActive);
    EEPROM.commit();
    Serial.print("NeoPixel is now ");
    Serial.println(neoPixelActive ? "Enabled" : "Disabled");

  } else if (option == 1) { 
    uint8_t brightnessPercent = map(oledBrightness, 0, 255, 0, 100); 
    brightnessPercent += 10; 
    if (brightnessPercent > 100) brightnessPercent = 0; 
    oledBrightness = map(brightnessPercent, 0, 100, 0, 255); 

    u8g2.setContrast(oledBrightness); 
    EEPROM.write(EEPROM_ADDRESS_BRIGHTNESS, oledBrightness);
    EEPROM.commit();

    Serial.print("Brightness set to: ");
    Serial.print(brightnessPercent);
    Serial.println("%");

    } else if (option == 2) {
    // আগের SD কার্ড কোড না করে OTA চালু করুন
    startOTAMode();
    current_screen = 0;
  }
}

void handleButtons() {
  if (!digitalRead(BUTTON_UP_PIN)) {
    if (!buttonUpPressed) {
      buttonUpPressed = true;
      currentOption = (currentOption - 1 + totalOptions) % totalOptions;
    }
  } else {
    buttonUpPressed = false;
  }

  if (!digitalRead(BUTTON_DOWN_PIN)) {
    if (!buttonDownPressed) {
      buttonDownPressed = true;
      currentOption = (currentOption + 1) % totalOptions;
    }
  } else {
    buttonDownPressed = false;
  }

  if (!digitalRead(BTN_PIN_RIGHT)) {
    if (!buttonSelectPressed) {
      buttonSelectPressed = true;
      toggleOption(currentOption);
    }
  } else {
    buttonSelectPressed = false;
  }
}

void displayMenu() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Settings:");

  u8g2.setFont(u8g2_font_5x8_tr);
  if (currentOption == 0) {
    u8g2.drawStr(0, 30, "> NeoPixel: ");
  } else {
    u8g2.drawStr(0, 30, "  NeoPixel: ");
  }

  if (currentOption == 1) {
    u8g2.drawStr(0, 45, "> Brightness: ");
  } else {
    u8g2.drawStr(0, 45, "  Brightness: ");
  }

  if (currentOption == 2) {
    u8g2.drawStr(0, 60, "> Update Firmware");
  } else {
    u8g2.drawStr(0, 60, "  Update Firmware");
  }

  u8g2.setCursor(80, 30);
  u8g2.print(neoPixelActive ? "Enabled" : "Disabled");

  u8g2.setCursor(80, 45);
  uint8_t brightnessPercent = map(oledBrightness, 0, 255, 0, 100);
  u8g2.print(brightnessPercent);
  u8g2.print("%");

  u8g2.sendBuffer();
}

void settingSetup() {
  Serial.begin(115200);

  EEPROM.begin(512);

  neoPixelActive = EEPROM.read(EEPROM_ADDRESS_NEOPIXEL);
  oledBrightness = EEPROM.read(EEPROM_ADDRESS_BRIGHTNESS);
  
  if (oledBrightness > 255) oledBrightness = 128; 
  u8g2.setContrast(oledBrightness);

  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_PIN_RIGHT, INPUT_PULLUP);
}

void settingLoop() {
  handleButtons();
  displayMenu();
  }
} 



bool getNtpTime(struct tm *timeinfo, uint32_t timeoutMs) {
    configTime(0, 0, "pool.ntp.org");
    struct tm tm;
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (getLocalTime(&tm)) {
            *timeinfo = tm;
            return true;
        }
        delay(100);
    }
    return false;
}