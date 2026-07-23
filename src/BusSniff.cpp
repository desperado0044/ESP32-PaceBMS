#include "BusSniff.h"

namespace BusSniff {

namespace {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
constexpr size_t kMaxBytes = 4096;
uint8_t buffer[kMaxBytes];
size_t bufferLen = 0;
bool activeFlag = false;
unsigned long startMs = 0;
unsigned long durationMsValue = 0;
}  // namespace

void start(unsigned long durationMs) {
    portENTER_CRITICAL(&mux);
    bufferLen = 0;
    portEXIT_CRITICAL(&mux);
    startMs = millis();
    durationMsValue = durationMs;
    activeFlag = true;
}

bool active() { return activeFlag; }

void collectIfActive(HardwareSerial& serial) {
    if (!activeFlag) return;
    while (serial.available()) {
        int b = serial.read();
        portENTER_CRITICAL(&mux);
        if (bufferLen < kMaxBytes) buffer[bufferLen++] = (uint8_t)b;
        portEXIT_CRITICAL(&mux);
    }
    if (millis() - startMs >= durationMsValue) activeFlag = false;
}

String resultText() {
    uint8_t localCopy[kMaxBytes];
    size_t len;
    portENTER_CRITICAL(&mux);
    len = bufferLen;
    memcpy(localCopy, buffer, len);
    portEXIT_CRITICAL(&mux);

    String out = (active() ? "laeuft noch... " : "fertig. ");
    out += String(len) + " Byte empfangen:\n";
    for (size_t i = 0; i < len; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X ", localCopy[i]);
        out += b;
    }
    return out;
}

}  // namespace BusSniff
