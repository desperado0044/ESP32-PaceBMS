#include "TouchCalibration.h"
#include "DisplayHardware.h"
#include "Config.h"
#include "StorageCalibration.h"

TouchCalibrationRoutine& TouchCalibrationRoutine::instance() {
    static TouchCalibrationRoutine routine;
    return routine;
}

bool TouchCalibrationRoutine::pollLocalTrigger(bool touchedNow) {
    if (!touchedNow) {
        touchStartMs = 0;
        return false;
    }
    if (touchStartMs == 0) {
        touchStartMs = millis();
        return false;
    }
    if (millis() - touchStartMs >= CALIBRATION_LOCAL_TRIGGER_HOLD_MS) {
        touchStartMs = 0;
        runSequence();
        return true;
    }
    return false;
}

void TouchCalibrationRoutine::runNow() { runSequence(); }

namespace {

struct RawPoint {
    int32_t x;
    int32_t y;
};

RawPoint waitForTouchAt(int16_t crossX, int16_t crossY) {
    tft.fillScreen(TFT_BLACK);
    tft.drawLine(crossX - 10, crossY, crossX + 10, crossY, TFT_WHITE);
    tft.drawLine(crossX, crossY - 10, crossX, crossY + 10, TFT_WHITE);

    // Blocking wait is fine here: calibration is a rare, explicit user interaction, not part of
    // the normal update loop. Wait for release first - relevant for the local hold-to-trigger
    // gesture, where the triggering touch is still physically active when runSequence() starts;
    // otherwise that stale touch (wherever it happened) would be misread as the first corner
    // reading.
    while (ts.touched()) delay(10);
    while (!ts.touched()) delay(10);
    TS_Point p = ts.getPoint();
    while (ts.touched()) delay(10);
    return RawPoint{p.x, p.y};
}

}  // namespace

void TouchCalibrationRoutine::runSequence() {
    const int16_t inset = 20;
    RawPoint tl = waitForTouchAt(inset, inset);
    RawPoint tr = waitForTouchAt(SCREEN_WIDTH - inset, inset);
    RawPoint bl = waitForTouchAt(inset, SCREEN_HEIGHT - inset);
    RawPoint br = waitForTouchAt(SCREEN_WIDTH - inset, SCREEN_HEIGHT - inset);

    // Averaged per edge (not global min/max across all 4 corners) so which raw value belongs to
    // which physical side stays known - map() below then handles an inverted touch axis correctly
    // too, instead of registering touches mirrored/rotated 180 degrees.
    TouchCalibration cal;
    cal.rawXLeft = (tl.x + bl.x) / 2;
    cal.rawXRight = (tr.x + br.x) / 2;
    cal.rawYTop = (tl.y + tr.y) / 2;
    cal.rawYBottom = (bl.y + br.y) / 2;
    CalibrationManager::instance().save(cal);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Kalibrierung gespeichert", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 2);
    tft.setTextDatum(TL_DATUM);
    delay(1000);
}

bool getCalibratedTouch(int16_t& outX, int16_t& outY) {
    if (!ts.touched()) return false;
    TS_Point p = ts.getPoint();
    TouchCalibration cal = CalibrationManager::instance().load();
    long x = map(p.x, cal.rawXLeft, cal.rawXRight, 0, SCREEN_WIDTH);
    long y = map(p.y, cal.rawYTop, cal.rawYBottom, 0, SCREEN_HEIGHT);
    outX = constrain(x, 0, SCREEN_WIDTH - 1);
    outY = constrain(y, 0, SCREEN_HEIGHT - 1);
    return true;
}
