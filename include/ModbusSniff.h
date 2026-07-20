#pragma once
#include <Arduino.h>

// One-off diagnostic: passively listen on the Modbus/RS485 UART (no transmit) and capture whatever
// raw bytes show up, independent of our own request/response protocol code - useful when our own
// reads time out and it's unclear whether that's a wiring/hardware issue or something in how we
// build/parse frames. The actual byte collection happens incrementally inside NetworkTask's own
// loop (see collectIfActive()) rather than in a blocking loop somewhere else - an earlier version
// blocked the AsyncWebServer/AsyncTCP callback context for several seconds to do this directly and
// tripped the ESP32's task watchdog (crashed the board). NetworkTask's loop already yields safely
// on every iteration, so piggybacking on it avoids that entirely.
namespace ModbusSniff {

// Starts a capture window: clears the buffer, marks active, records start time + duration.
void start(unsigned long durationMs);

bool active();

// Called from NetworkTask's own loop every iteration while active() - reads whatever bytes are
// currently available on the given serial port into the internal buffer, and auto-deactivates once
// the requested duration has elapsed. Does not transmit anything.
void collectIfActive(HardwareSerial& serial);

// Human-readable snapshot of the capture so far (hex bytes, byte count, still-active flag) -
// safe to call anytime, including while a capture is still running.
String resultText();

}  // namespace ModbusSniff
