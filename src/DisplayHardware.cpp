#include "DisplayHardware.h"
#include "Config.h"

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

namespace DisplayHardware {

void setBrightness(uint8_t value) { ledcWrite(TFT_BACKLIGHT_LEDC_CHANNEL, value); }

void begin() {
    tft.init();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BLACK);

    // Backlight driven manually via ledc PWM (not TFT_eSPI's TFT_BL flag) so brightness can be
    // adjusted later instead of only switched on/off.
    ledcSetup(TFT_BACKLIGHT_LEDC_CHANNEL, TFT_BACKLIGHT_LEDC_FREQ_HZ, TFT_BACKLIGHT_LEDC_RESOLUTION_BITS);
    ledcAttachPin(TFT_BACKLIGHT_PIN, TFT_BACKLIGHT_LEDC_CHANNEL);
    setBrightness(TFT_BACKLIGHT_DEFAULT);

    ts.begin();
    ts.setRotation(TFT_ROTATION);
}

}  // namespace DisplayHardware
