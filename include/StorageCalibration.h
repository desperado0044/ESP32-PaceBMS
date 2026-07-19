#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Raw XPT2046 readings at the four screen edges, persisted in NVS so the 4-corner calibration
// routine only needs to run once per physical unit. Stored as "raw value at left/right/top/bottom
// edge" rather than plain min/max, since which edge yields the numerically smaller value depends
// on the touch panel's (unpredictable) axis direction - map() in TouchCalibration.cpp handles an
// inverted axis correctly either way.
struct TouchCalibration {
    uint16_t rawXLeft;
    uint16_t rawXRight;
    uint16_t rawYTop;
    uint16_t rawYBottom;
};

class CalibrationManager {
public:
    static CalibrationManager& instance();
    bool begin();

    TouchCalibration load();
    void save(const TouchCalibration& cal);

private:
    CalibrationManager() = default;
    Preferences prefs;
};
