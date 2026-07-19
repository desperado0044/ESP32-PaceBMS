#pragma once

// Physical factory reset via the ESP32 board's BOOT/FLASH button (see Config.h for the exact
// pin/hold-duration and the rationale for using a button instead of a touch gesture).
namespace FactoryReset {

// Configures the button pin. Call once from setup().
void begin();

// Call every loop() tick. Blocks (briefly, for on-screen feedback) and reboots if the hold
// duration is reached - never returns in that case.
void checkButton();

}  // namespace FactoryReset
