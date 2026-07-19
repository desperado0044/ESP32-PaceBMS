#pragma once
#include "BmsData.h"

// Touchscreen dashboard for the BMS snapshot: tab-based navigation (Uebersicht / Zellen / Status /
// Steuerung), redrawn only when the page changes, new BMS data arrives, or a periodic heartbeat
// elapses (keeps SPI traffic low - the BMS itself is only polled every few seconds anyway).
namespace BmsDisplayUi {

void begin();

// Call every loop() tick; handles touch input/navigation internally.
void update(const PaceBmsSnapshot& snapshot);

}  // namespace BmsDisplayUi
