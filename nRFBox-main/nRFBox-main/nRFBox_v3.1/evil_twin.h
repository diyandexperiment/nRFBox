#ifndef EVIL_TWIN_H
#define EVIL_TWIN_H

#include "config.h"
#include <DNSServer.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <vector>

extern int attackModeIndex;

namespace EvilTwin {

    // ==================== এনাম ====================
    enum MenuState {
        MENU_MAIN,
        MENU_SCAN,
        MENU_ATTACK_MODE,
        MENU_TARGET_SELECT,
        MENU_CREDENTIALS,
        MENU_MONITOR
    };

    enum AttackMode {
        NONE,
        DEAUTH_ONLY,
        EVIL_TWIN_ONLY,
        EVIL_TWIN_DEAUTH
    };

    enum VerifyMode {
        VERIFY_OFF,
        VERIFY_ON
    };

    // ==================== ফাংশন ====================
    void evilTwinSetup();
    void evilTwinLoop();

    // OLED ডিসপ্লে ফাংশন
    void drawMainMenu();
    void drawScanList();
    void drawAttackModeMenu();
    void drawAttackStatus();
    void drawCredentials();
    void drawMonitorScreen();

    // অ্যাটাক কন্ট্রোল
    void startAttack();
    void stopAttack();
    void startDeauth();
    void startEvilTwin();
    void startCombinedAttack();

    // পাসওয়ার্ড ভেরিফিকেশন
    bool verifyPassword(String ssid, String password);
    void captureCredential(String ssid, String password, bool verified);

    // ক্লায়েন্ট মনিটরিং
    void updateClientList();
    int getConnectedClientCount();
    String getClientInfo();

    // স্ক্যান
    bool scanNetworks();

    // বাটন হ্যান্ডলিং
    void handleButtons();
}

#endif