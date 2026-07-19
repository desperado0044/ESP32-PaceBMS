#pragma once
#include <Arduino.h>
#include "BmsData.h"

// High-level PACE BMS RS232 client: builds/sends the read commands ported from
// github.com/Tertiush/bmspace and parses the responses into BmsData structs.
// Write/control commands are not part of the upstream reference and are not
// implemented here yet.
class PaceBmsClient {
public:
    explicit PaceBmsClient(HardwareSerial& serial, unsigned long responseTimeoutMs = 500);

    // Runs the same read sequence as the upstream script (version+serial once,
    // then analog data / pack capacity / warning info every call) and fills
    // snapshot in place. Returns true if every step succeeded.
    bool poll(PaceBmsSnapshot& snapshot);

    const String& lastError() const { return lastError_; }

private:
    HardwareSerial& serial_;
    unsigned long responseTimeoutMs_;
    String lastError_;

    bool identityRead_ = false;

    bool sendAndReceive(const char* cid2, const char* infoAscii,
                         const uint8_t*& info, size_t& infoLen);
    bool readFrame(uint8_t* buf, size_t cap, size_t& outLen);

    bool readVersion(String& outVersion);
    bool readSerials(String& outBmsSerial, String& outPackSerial);
    bool readAnalogData(PaceBmsSnapshot& snapshot);
    bool readPackCapacity(PaceCapacity& outCapacity);
    bool readWarnInfo(PaceBmsSnapshot& snapshot);

    // Response buffer, reused across requests.
    static constexpr size_t kResponseBufCap = 512;
    uint8_t responseBuf_[kResponseBufCap];
};
