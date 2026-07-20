#pragma once

// Cross-core activity timestamps for the display's request/response indicator dot - set by
// NetworkTask (Core 0) right before/after each BMS poll, read by BmsDisplayUi (Core 1) to derive
// the dot's current color. Plain millis() timestamps, not snapshot data, so this deliberately
// doesn't go through SnapshotStore's mutex - single aligned 32-bit reads/writes are atomic on
// ESP32, and a stale-by-one-frame read here just means the dot flips color a few ms late, not a
// correctness bug the way torn String data in PaceBmsSnapshot would be.
namespace BmsActivity {

void markRequestSent();
void markResponseReceived();

unsigned long lastRequestMs();
unsigned long lastResponseMs();

}  // namespace BmsActivity
