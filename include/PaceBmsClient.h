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
    // then analog data / warning info every call) and fills snapshot in place.
    // Returns true if every step succeeded. Stack-wide capacity/SOC/SOH
    // (snapshot.capacity) is computed by NetworkTask from the per-pack data
    // afterward, not read here - the dedicated RS232 "pack capacity" command
    // was dropped because it unreliably always answered with pack 1's data
    // regardless of the real pack count (per Tertiush/bmspace's own notes).
    bool poll(PaceBmsSnapshot& snapshot);

    const String& lastError() const { return lastError_; }

    // Raw bytes seen on the wire during the most recent readFrame() call, as a hex string -
    // populated on every poll (success or timeout), unlike lastError() which only carries a raw
    // dump on timeout. Lets the web UI show "what did the BMS just send" for RS232, where (unlike
    // Modbus's shared bus) there's no independent passive traffic to sniff - only our own
    // request/response pairs exist at all. poll() makes two such calls per cycle (analog data,
    // then warn info), so by the time poll() returns this always holds the *last* one made (warn
    // info) - see lastAnalogRawHex() for the analog-data one specifically, which would otherwise
    // already be overwritten by the time anything outside this class can look at it.
    const String& lastRawHex() const { return lastRawHex_; }
    const String& lastAnalogRawHex() const { return lastAnalogRawHex_; }

    // See RuntimeSettings::rawCaptureEnabled() - off by default, since building the hex strings
    // above costs heap-allocating String work on every single poll cycle. NetworkTask sets this
    // from the runtime setting once per loop iteration; while off, lastRawHex()/lastAnalogRawHex()
    // just stay empty instead of being rebuilt.
    void setRawCaptureEnabled(bool enabled) { rawCaptureEnabled_ = enabled; }

private:
    HardwareSerial& serial_;
    unsigned long responseTimeoutMs_;
    String lastError_;
    String lastRawHex_;
    String lastAnalogRawHex_;
    bool rawCaptureEnabled_ = false;

    bool identityRead_ = false;

    bool sendAndReceive(const char* cid2, const char* infoAscii,
                         const uint8_t*& info, size_t& infoLen);
    bool readFrame(uint8_t* buf, size_t cap, size_t& outLen);
    void updateLastRawHex(const uint8_t* raw, size_t rawLen);

    bool readVersion(String& outVersion);
    bool readSerials(String& outBmsSerial, String& outPackSerial);
    bool readAnalogData(PaceBmsSnapshot& snapshot);
    bool readWarnInfo(PaceBmsSnapshot& snapshot);

    // Response buffer, reused across requests.
    static constexpr size_t kResponseBufCap = 512;
    uint8_t responseBuf_[kResponseBufCap];
};
