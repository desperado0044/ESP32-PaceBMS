#include "FactoryReset.h"
#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"
#include "DisplayHardware.h"

namespace FactoryReset {

namespace {
unsigned long pressStartMs = 0;

void eraseNamespace(const char* ns) {
    Preferences prefs;
    prefs.begin(ns, false);
    prefs.clear();
    prefs.end();
}
}  // namespace

void begin() { pinMode(FACTORY_RESET_BUTTON_PIN, INPUT_PULLUP); }

void checkButton() {
    bool pressed = digitalRead(FACTORY_RESET_BUTTON_PIN) == LOW;
    if (!pressed) {
        pressStartMs = 0;
        return;
    }
    if (pressStartMs == 0) {
        pressStartMs = millis();
        return;
    }
    if (millis() - pressStartMs < FACTORY_RESET_HOLD_MS) return;

    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("Werksreset...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

    // Fresh Preferences handles for each namespace rather than reusing CredentialsManager's/
    // CalibrationManager's already-open ones - simpler, and this runs right before a hard reboot
    // anyway so there's no ongoing state on this core to keep consistent with.
    eraseNamespace(CREDENTIALS_NVS_NAMESPACE);
    eraseNamespace(CALIBRATION_NVS_NAMESPACE);

    tft.drawString("Zurueckgesetzt, Neustart...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 20);
    tft.setTextDatum(TL_DATUM);
    delay(1000);
    ESP.restart();
}

}  // namespace FactoryReset
