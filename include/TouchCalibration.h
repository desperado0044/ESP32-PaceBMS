#pragma once
#include <Arduino.h>

// 4-corner crosshair calibration, ported from the Haustuerklingel project. Triggered locally by
// holding a single continuous touch anywhere on the screen for CALIBRATION_LOCAL_TRIGGER_HOLD_MS
// - independent of WiFi/MQTT, so it always works even on a freshly wired-up unit. A sustained
// hold was chosen (over e.g. N taps within a time window) because normal swipe/tab-tap use during
// everyday operation was triggering a tap-counting trigger by accident.
class TouchCalibrationRoutine {
public:
    static TouchCalibrationRoutine& instance();

    // Feed with the raw touched() value on every loop tick. Returns true if the local trigger just
    // fired (calibration already ran synchronously) - the caller should redraw.
    bool pollLocalTrigger(bool touchedNow);

    void runNow();

private:
    TouchCalibrationRoutine() = default;
    void runSequence();

    unsigned long touchStartMs = 0;  // 0 = not currently tracking a continuous touch
};

// Applies the persisted calibration to the current raw touch reading, in screen coordinates. The
// only touch read entry point for normal operation - the calibration routine itself reads
// ts.getPoint() directly since it needs uncalibrated raw values.
bool getCalibratedTouch(int16_t& outX, int16_t& outY);
