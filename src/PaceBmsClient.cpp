#include "PaceBmsClient.h"
#include "PaceBmsProtocol.h"

namespace {
constexpr const char* kVer = "25";
constexpr const char* kAdr = "01";
constexpr const char* kCid1 = "46";

constexpr const char* kCid2PackNumber = "90";
constexpr const char* kCid2PackAnalogData = "42";
constexpr const char* kCid2SoftwareVersion = "C1";
constexpr const char* kCid2SerialNumber = "C2";
constexpr const char* kCid2WarnInfo = "44";

// warningStates from constants.py: keys are literal 2-char ASCII codes.
const char* warningStateText(const uint8_t* pair) {
    if (memcmp(pair, "00", 2) == 0) return nullptr;
    if (memcmp(pair, "01", 2) == 0) return "below limit";
    if (memcmp(pair, "02", 2) == 0) return "above limit";
    if (memcmp(pair, "F0", 2) == 0) return "other fault";
    return "unknown warning";
}

String hexToAscii(const uint8_t* data, size_t len) {
    String out;
    out.reserve(len / 2);
    for (size_t i = 0; i + 1 < len; i += 2) {
        char buf[3] = {(char)data[i], (char)data[i + 1], 0};
        out += (char)strtol(buf, nullptr, 16);
    }
    return out;
}

}  // namespace

PaceBmsClient::PaceBmsClient(HardwareSerial& serial, unsigned long responseTimeoutMs)
    : serial_(serial), responseTimeoutMs_(responseTimeoutMs) {}

bool PaceBmsClient::readFrame(uint8_t* buf, size_t cap, size_t& outLen) {
    unsigned long start = millis();
    size_t len = 0;
    bool started = false;

    // Diagnostic only: every byte actually seen on the wire, including anything before SOI is
    // found (which the real parse below discards without ever recording) - so a timeout can report
    // exactly what (if anything) arrived, over the network, without a USB/serial connection.
    constexpr size_t kRawCap = kResponseBufCap;
    uint8_t raw[kRawCap];
    size_t rawLen = 0;

    while (millis() - start < responseTimeoutMs_) {
        while (serial_.available()) {
            int c = serial_.read();
            if (rawLen < kRawCap) raw[rawLen++] = (uint8_t)c;
            if (!started) {
                if (c != (int)PaceBmsProtocol::SOI) continue;
                started = true;
            }
            if (len >= cap) {
                lastError_ = "Response frame exceeds buffer size";
                updateLastRawHex(raw, rawLen);
                return false;
            }
            buf[len++] = (uint8_t)c;
            if (c == (int)PaceBmsProtocol::EOI) {
                outLen = len;
                updateLastRawHex(raw, rawLen);
                return true;
            }
        }
    }
    lastError_ = "Timeout waiting for BMS response (" + String(rawLen) + " Byte gesehen: ";
    for (size_t i = 0; i < rawLen; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X ", raw[i]);
        lastError_ += b;
    }
    lastError_ += ")";
    updateLastRawHex(raw, rawLen);
    return false;
}

void PaceBmsClient::updateLastRawHex(const uint8_t* raw, size_t rawLen) {
    lastRawHex_ = "";
    if (!rawCaptureEnabled_) return;
    for (size_t i = 0; i < rawLen; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X ", raw[i]);
        lastRawHex_ += b;
    }
}

bool PaceBmsClient::sendAndReceive(const char* cid2, const char* infoAscii,
                                    const uint8_t*& info, size_t& infoLen) {
    uint8_t request[64];
    size_t requestLen = PaceBmsProtocol::buildRequest(kVer, kAdr, kCid1, cid2, infoAscii,
                                                       request, sizeof(request));
    if (requestLen == 0) {
        lastError_ = "Failed to build request frame";
        return false;
    }

    while (serial_.available()) serial_.read();  // drop stale bytes before requesting
    serial_.write(request, requestLen);
    serial_.flush();

    size_t responseLen = 0;
    if (!readFrame(responseBuf_, kResponseBufCap, responseLen)) return false;

    PaceBmsProtocol::ParsedResponse resp =
        PaceBmsProtocol::parseResponse(responseBuf_, responseLen);
    if (!resp.ok) {
        lastError_ = resp.error ? resp.error : "Unknown protocol error";
        return false;
    }

    info = resp.info;
    infoLen = resp.infoLen;
    return true;
}

bool PaceBmsClient::readVersion(String& outVersion) {
    const uint8_t* info;
    size_t infoLen;
    if (!sendAndReceive(kCid2SoftwareVersion, nullptr, info, infoLen)) return false;
    outVersion = hexToAscii(info, infoLen);
    return true;
}

bool PaceBmsClient::readSerials(String& outBmsSerial, String& outPackSerial) {
    const uint8_t* info;
    size_t infoLen;
    if (!sendAndReceive(kCid2SerialNumber, nullptr, info, infoLen)) return false;
    if (infoLen < 68) {
        lastError_ = "Serial number response too short";
        return false;
    }
    outBmsSerial = hexToAscii(info, 30);
    outBmsSerial.replace(" ", "");
    outPackSerial = hexToAscii(info + 40, 28);
    outPackSerial.replace(" ", "");
    return true;
}

bool PaceBmsClient::readAnalogData(PaceBmsSnapshot& snapshot) {
    const uint8_t* info;
    size_t infoLen;
    bool sent = sendAndReceive(kCid2PackAnalogData, "FF", info, infoLen);
    // Stashed before readWarnInfo()'s own call overwrites lastRawHex_ - see lastAnalogRawHex().
    lastAnalogRawHex_ = lastRawHex_;
    if (!sent) return false;

    size_t pos = 2;  // upstream skips 2 leading chars before the pack count
    long packs = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
    if (packs < 0 || packs > PACE_MAX_PACKS) {
        lastError_ = "Unsupported or invalid pack count in analog data";
        return false;
    }
    uint8_t reportedCount = (uint8_t)packs;
    uint8_t previousCount = snapshot.packCount;

    for (int p = 0; p < reportedCount; p++) {
        PacePackAnalog& pack = snapshot.packs[p];
        snapshot.packAddress[p] = p + 1;  // RS232 has no separate physical address, just a slot number

        long cells = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        if (cells < 0 || cells > PACE_MAX_CELLS) {
            lastError_ = "Invalid cell count in analog data";
            return false;
        }
        pack.cellCount = (uint8_t)cells;

        uint16_t minMv = 0, maxMv = 0;
        for (int i = 0; i < cells; i++) {
            long mv = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
            if (mv < 0) { lastError_ = "Truncated cell voltage"; return false; }
            pack.cellMillivolts[i] = (uint16_t)mv;
            if (i == 0) { minMv = maxMv = (uint16_t)mv; }
            else {
                if ((uint16_t)mv < minMv) minMv = (uint16_t)mv;
                if ((uint16_t)mv > maxMv) maxMv = (uint16_t)mv;
            }
        }
        pack.cellMaxDiffMv = maxMv - minMv;

        long temps = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        if (temps < 0 || temps > PACE_MAX_TEMPS) {
            lastError_ = "Invalid temperature sensor count in analog data";
            return false;
        }
        pack.tempCount = (uint8_t)temps;
        for (int i = 0; i < temps; i++) {
            long raw = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
            if (raw < 0) { lastError_ = "Truncated temperature"; return false; }
            pack.temperaturesC[i] = (raw - 2730) / 10.0f;
        }

        long iPackRaw = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
        if (iPackRaw < 0) { lastError_ = "Truncated pack current"; return false; }
        int32_t iPack = (uint16_t)iPackRaw >= 32768 ? -(65535 - iPackRaw) : iPackRaw;
        pack.packCurrentA = iPack / 100.0f;

        long vPackRaw = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
        if (vPackRaw < 0) { lastError_ = "Truncated pack voltage"; return false; }
        pack.packVoltageV = vPackRaw / 1000.0f;

        long remainCap = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
        if (remainCap < 0) { lastError_ = "Truncated remaining capacity"; return false; }
        pack.remainingCapacityMah = (uint32_t)remainCap * 10;

        pos += 2;  // "Define number P = 3" field, unused

        long fullCap = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
        if (fullCap < 0) { lastError_ = "Truncated full capacity"; return false; }
        pack.fullCapacityMah = (uint32_t)fullCap * 10;

        pack.socPercent = pack.fullCapacityMah > 0
                               ? (pack.remainingCapacityMah * 100.0f) / pack.fullCapacityMah
                               : 0;

        long cycles = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
        if (cycles < 0) { lastError_ = "Truncated cycle count"; return false; }
        pack.cycles = (uint16_t)cycles;

        long designCap = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
        if (designCap < 0) { lastError_ = "Truncated design capacity"; return false; }
        pack.designCapacityMah = (uint32_t)designCap * 10;

        pack.sohPercent = pack.designCapacityMah > 0
                               ? (pack.fullCapacityMah * 100.0f) / pack.designCapacityMah
                               : 0;

        pos += 2;  // trailing field, meaning undocumented upstream

        // Optional INFOFLAG byte between packs; skip it if present.
        if (pos < infoLen) {
            size_t peekPos = pos;
            long nextVal = PaceBmsProtocol::readHexField(info, infoLen, peekPos, 2);
            if (nextVal != cells) pos += 2;
        }
    }

    // A pack that stopped reporting is zeroed rather than left showing its last (now stale)
    // reading, but the slot itself keeps being shown/published - packCount only ever grows, it
    // never shrinks back down on its own.
    for (uint8_t i = reportedCount; i < previousCount; i++) {
        snapshot.packs[i] = PacePackAnalog();
        snapshot.warn[i] = PacePackWarn();
    }
    snapshot.packCount = reportedCount > previousCount ? reportedCount : previousCount;

    return true;
}

bool PaceBmsClient::readWarnInfo(PaceBmsSnapshot& snapshot) {
    const uint8_t* info;
    size_t infoLen;
    if (!sendAndReceive(kCid2WarnInfo, "FF", info, infoLen)) return false;

    size_t pos = 2;
    long packsW = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
    // Must use THIS response's own pack-count field, not snapshot.packCount - the latter never
    // shrinks (see readAnalogData()'s shrink-handling comment), so once a pack drops out and the
    // master starts reporting fewer packs, snapshot.packCount would still claim the old, higher
    // count and this loop would keep trying to parse warning data for a pack that's no longer in
    // the response at all, running off the end of the buffer ("Truncated cell warning count") on
    // every single cycle from then on - reproduced on real hardware after a pack disconnected.
    if (packsW < 0 || packsW > PACE_MAX_PACKS) {
        lastError_ = "Truncated warning info header";
        return false;
    }
    uint8_t packCount = (uint8_t)packsW;

    for (int p = 0; p < packCount && p < PACE_MAX_PACKS; p++) {
        PacePackWarn& warn = snapshot.warn[p];
        warn.warnings = "";

        long cellsW = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        if (cellsW < 0) { lastError_ = "Truncated cell warning count"; return false; }

        for (int c = 0; c < cellsW; c++) {
            if (pos + 2 > infoLen) { lastError_ = "Truncated cell warning data"; return false; }
            const char* text = warningStateText(info + pos);
            if (text) { warn.warnings += "cell " + String(c + 1) + " " + text + ", "; }
            pos += 2;
        }

        long tempsW = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        if (tempsW < 0) { lastError_ = "Truncated temp warning count"; return false; }

        for (int t = 0; t < tempsW; t++) {
            if (pos + 2 > infoLen) { lastError_ = "Truncated temp warning data"; return false; }
            const char* text = warningStateText(info + pos);
            if (text) { warn.warnings += "temp " + String(t + 1) + " " + text + ", "; }
            pos += 2;
        }

        if (pos + 2 > infoLen) { lastError_ = "Truncated warning info"; return false; }
        if (const char* t = warningStateText(info + pos)) warn.warnings += String("charge current ") + t + ", ";
        pos += 2;
        if (const char* t = warningStateText(info + pos)) warn.warnings += String("total voltage ") + t + ", ";
        pos += 2;
        if (const char* t = warningStateText(info + pos)) warn.warnings += String("discharge current ") + t + ", ";
        pos += 2;

        long protectState1 = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        warn.protShortCircuit = (protectState1 >> 6) & 1;
        warn.protDischargeCurrent = (protectState1 >> 5) & 1;
        warn.protChargeCurrent = (protectState1 >> 4) & 1;

        long protectState2 = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        warn.fullyCharged = (protectState2 >> 7) & 1;

        long instructionState = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        warn.currentLimitOn = (instructionState >> 0) & 1;
        warn.chargeFetOn = (instructionState >> 1) & 1;
        warn.dischargeFetOn = (instructionState >> 2) & 1;
        warn.packIndicateOn = (instructionState >> 3) & 1;
        warn.reverseOn = (instructionState >> 4) & 1;
        warn.acInOn = (instructionState >> 5) & 1;
        warn.heartOn = (instructionState >> 7) & 1;

        PaceBmsProtocol::readHexField(info, infoLen, pos, 2);  // controlState, text-only upstream

        PaceBmsProtocol::readHexField(info, infoLen, pos, 2);  // faultState, text-only upstream

        long balance1 = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        long balance2 = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        warn.balanceState1 = (uint8_t)balance1;
        warn.balanceState2 = (uint8_t)balance2;

        PaceBmsProtocol::readHexField(info, infoLen, pos, 2);  // warnState1, text-only upstream
        PaceBmsProtocol::readHexField(info, infoLen, pos, 2);  // warnState2, text-only upstream

        warn.warnings.trim();
        while (warn.warnings.endsWith(",")) {
            warn.warnings.remove(warn.warnings.length() - 1);
            warn.warnings.trim();
        }

        // Optional INFOFLAG byte between packs; skip it if present.
        if (pos < infoLen) {
            size_t peekPos = pos;
            long nextVal = PaceBmsProtocol::readHexField(info, infoLen, peekPos, 2);
            if (nextVal != cellsW) pos += 2;
        }
    }

    return true;
}

bool PaceBmsClient::poll(PaceBmsSnapshot& snapshot) {
    if (!identityRead_) {
        String version, bmsSerial, packSerial;
        if (readVersion(version)) snapshot.bmsVersion = version;
        if (readSerials(bmsSerial, packSerial)) {
            snapshot.bmsSerial = bmsSerial;
            snapshot.packSerial = packSerial;
            identityRead_ = true;
        }
    }

    bool ok = true;
    if (!readAnalogData(snapshot)) ok = false;
    if (!readWarnInfo(snapshot)) ok = false;

    if (ok) {
        snapshot.valid = true;
        snapshot.lastUpdateMs = millis();
    }
    return ok;
}
