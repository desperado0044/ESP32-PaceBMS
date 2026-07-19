#include "StorageCalibration.h"
#include "Config.h"

CalibrationManager& CalibrationManager::instance() {
    static CalibrationManager manager;
    return manager;
}

bool CalibrationManager::begin() { return prefs.begin(CALIBRATION_NVS_NAMESPACE, false); }

TouchCalibration CalibrationManager::load() {
    TouchCalibration cal;
    cal.rawXLeft = prefs.getUShort("x_left", TOUCH_DEFAULT_MINX);
    cal.rawXRight = prefs.getUShort("x_right", TOUCH_DEFAULT_MAXX);
    cal.rawYTop = prefs.getUShort("y_top", TOUCH_DEFAULT_MINY);
    cal.rawYBottom = prefs.getUShort("y_bottom", TOUCH_DEFAULT_MAXY);
    return cal;
}

void CalibrationManager::save(const TouchCalibration& cal) {
    prefs.putUShort("x_left", cal.rawXLeft);
    prefs.putUShort("x_right", cal.rawXRight);
    prefs.putUShort("y_top", cal.rawYTop);
    prefs.putUShort("y_bottom", cal.rawYBottom);
}
