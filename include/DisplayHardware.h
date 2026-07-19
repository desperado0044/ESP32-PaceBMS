#pragma once
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// Shared display/touch instances, defined once in DisplayHardware.cpp. tft uses TFT_eSPI's own
// HSPI-based SPI instance (USE_HSPI_PORT build flag); ts uses the global Arduino `SPI` object on
// its native VSPI pins - genuinely separate hardware from the display bus (see platformio.ini).
extern TFT_eSPI tft;
extern XPT2046_Touchscreen ts;

namespace DisplayHardware {

// Initializes TFT + backlight PWM + touch controller. Call once from setup().
void begin();

void setBrightness(uint8_t value);

}  // namespace DisplayHardware
