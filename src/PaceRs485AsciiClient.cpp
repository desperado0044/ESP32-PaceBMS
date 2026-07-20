#include "PaceRs485AsciiClient.h"
#include "PaceBmsProtocol.h"
#include "Config.h"
#include "RuntimeSettings.h"

namespace {
constexpr const char* kVer = "25";
constexpr const char* kCid1 = "46";
constexpr const char* kCid2Analog = "42";
constexpr const char* kCid2Warn = "44";

const char* warningStateText(const uint8_t* pair) {
    if (memcmp(pair, "00", 2) == 0) return nullptr;
    if (memcmp(pair, "01", 2) == 0) return "below limit";
    if (memcmp(pair, "02", 2) == 0) return "above limit";
    if (memcmp(pair, "F0", 2) == 0) return "other fault";
    return "unknown warning";
}

String hexDump(const uint8_t* data, size_t len) {
    String out = " (" + String(len) + " Byte INFO: ";
    for (size_t i = 0; i < len; i++) out += (char)data[i];
    out += ")";
    return out;
}
}  // namespace

PaceRs485AsciiClient::PaceRs485AsciiClient(HardwareSerial& serial, int dePin,
                                            unsigned long responseTimeoutMs)
    : serial_(serial), dePin_(dePin), responseTimeoutMs_(responseTimeoutMs) {}

bool PaceRs485AsciiClient::readFrame(uint8_t* buf, size_t cap, size_t& outLen) {
    unsigned long start = millis();
    size_t len = 0;
    bool started = false;

    while (millis() - start < responseTimeoutMs_) {
        while (serial_.available()) {
            int c = serial_.read();
            if (!started) {
                if (c != (int)PaceBmsProtocol::SOI) continue;
                started = true;
            }
            if (len >= cap) {
                lastError_ = "Response frame exceeds buffer size";
                return false;
            }
            buf[len++] = (uint8_t)c;
            if (c == (int)PaceBmsProtocol::EOI) {
                outLen = len;
                return true;
            }
        }
    }
    lastError_ = "Timeout waiting for RS485 response";
    return false;
}

bool PaceRs485AsciiClient::sendAndReceive(uint8_t targetAddr, const char* cid2,
                                          const uint8_t*& info, size_t& infoLen) {
    lastError_ = "";  // cleared up front so a stale error from a previous address/query never
                      // leaks into this attempt's result

    // Minimum gap before every request (not just between analog/warn for the same pack) - matches
    // the request_throttle value the nkinnan/esphome-pace-bms project documents needing for this
    // same ASCII protocol version (VER=25): back-to-back requests with no gap reliably timed out.
    delay(50);

    char adrStr[3];
    snprintf(adrStr, sizeof(adrStr), "%02X", targetAddr);

    uint8_t request[32];
    // ADR = target pack's real address, COMMAND (INFO) = same address again - matches the exact
    // exchange observed on a passive bus sniff of the master polling its own slave packs this way.
    size_t requestLen = PaceBmsProtocol::buildRequest(kVer, adrStr, kCid1, cid2, adrStr, request,
                                                       sizeof(request));
    if (requestLen == 0) {
        lastError_ = "Failed to build request frame";
        return false;
    }

    while (serial_.available()) serial_.read();  // drop stale bytes before requesting

    digitalWrite(dePin_, HIGH);  // enable RS485 driver (transmit)
    serial_.write(request, requestLen);
    serial_.flush();
    digitalWrite(dePin_, LOW);  // back to receive

    size_t responseLen = 0;
    if (!readFrame(responseBuf_, kResponseBufCap, responseLen)) return false;

    PaceBmsProtocol::ParsedResponse resp =
        PaceBmsProtocol::parseResponse(responseBuf_, responseLen);
    if (!resp.ok) {
        lastError_ = resp.error ? resp.error : "Unknown protocol error";
        // Raw bytes appended so a format mismatch can be diagnosed over the network.
        lastError_ += " (" + String(responseLen) + " Byte: ";
        for (size_t i = 0; i < responseLen; i++) {
            char b[4];
            snprintf(b, sizeof(b), "%02X ", responseBuf_[i]);
            lastError_ += b;
        }
        lastError_ += ")";
        return false;
    }

    info = resp.info;
    infoLen = resp.infoLen;
    return true;
}

bool PaceRs485AsciiClient::pollPack(uint8_t targetAddr, PacePackAnalog& pack, PacePackWarn& warn) {
    const uint8_t* info;
    size_t infoLen;
    bool ok = true;

    if (sendAndReceive(targetAddr, kCid2Analog, info, infoLen)) {
        // Header: 1 byte INFOFLAG + 1 byte echoed COMMAND (same 2-byte prefix PaceBmsClient's
        // FFH/all-packs response has before its per-pack data block starts) - then exactly one
        // pack's worth of Chart A.14 data, since this response is for a single addressed pack.
        size_t pos = 4;

        long cells = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        if (cells < 0 || cells > PACE_MAX_CELLS) {
            lastError_ = "Invalid cell count in single-pack analog data" + hexDump(info, infoLen);
            ok = false;
        } else {
            pack.cellCount = (uint8_t)cells;
            uint16_t minMv = 0, maxMv = 0;
            for (int i = 0; i < cells; i++) {
                long mv = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
                if (mv < 0) { lastError_ = "Truncated cell voltage"; ok = false; break; }
                pack.cellMillivolts[i] = (uint16_t)mv;
                if (i == 0) { minMv = maxMv = (uint16_t)mv; }
                else {
                    if ((uint16_t)mv < minMv) minMv = (uint16_t)mv;
                    if ((uint16_t)mv > maxMv) maxMv = (uint16_t)mv;
                }
            }
            pack.cellMaxDiffMv = maxMv - minMv;

            if (ok) {
                long temps = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
                if (temps < 0 || temps > PACE_MAX_TEMPS) {
                    lastError_ = "Invalid temperature sensor count in single-pack analog data";
                    ok = false;
                } else {
                    pack.tempCount = (uint8_t)temps;
                    for (int i = 0; i < temps; i++) {
                        long raw = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
                        if (raw < 0) { lastError_ = "Truncated temperature"; ok = false; break; }
                        pack.temperaturesC[i] = (raw - 2730) / 10.0f;
                    }
                }
            }

            if (ok) {
                long iPackRaw = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
                long vPackRaw = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
                long remainCap = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
                pos += 2;  // "Define number P = 3" field, unused
                long fullCap = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
                long cycles = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
                long designCap = PaceBmsProtocol::readHexField(info, infoLen, pos, 4);
                if (iPackRaw < 0 || vPackRaw < 0 || remainCap < 0 || fullCap < 0 || cycles < 0 ||
                    designCap < 0) {
                    lastError_ = "Truncated pack analog tail fields" + hexDump(info, infoLen);
                    ok = false;
                } else {
                    int32_t iPack = (uint16_t)iPackRaw >= 32768 ? -(65535 - iPackRaw) : iPackRaw;
                    pack.packCurrentA = iPack / 100.0f;
                    pack.packVoltageV = vPackRaw / 1000.0f;
                    pack.remainingCapacityMah = (uint32_t)remainCap * 10;
                    pack.fullCapacityMah = (uint32_t)fullCap * 10;
                    pack.cycles = (uint16_t)cycles;
                    pack.designCapacityMah = (uint32_t)designCap * 10;
                    pack.socPercent = pack.fullCapacityMah > 0
                                           ? (pack.remainingCapacityMah * 100.0f) / pack.fullCapacityMah
                                           : 0;
                    pack.sohPercent = pack.designCapacityMah > 0
                                           ? (pack.fullCapacityMah * 100.0f) / pack.designCapacityMah
                                           : 0;
                }
            }
        }
    } else {
        ok = false;
    }

    String analogError = lastError_;
    bool warnOk = true;
    if (sendAndReceive(targetAddr, kCid2Warn, info, infoLen)) {
        warn.warnings = "";
        size_t pos = 4;  // same 2-byte INFOFLAG+COMMAND-echo header as the analog response

        long cellsW = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
        if (cellsW < 0) { lastError_ = "Truncated cell warning count" + hexDump(info, infoLen); warnOk = false; }

        for (int c = 0; warnOk && c < cellsW; c++) {
            if (pos + 2 > infoLen) { lastError_ = "Truncated cell warning data"; warnOk = false; break; }
            const char* text = warningStateText(info + pos);
            if (text) { warn.warnings += "cell " + String(c + 1) + " " + text + ", "; }
            pos += 2;
        }

        if (warnOk) {
            long tempsW = PaceBmsProtocol::readHexField(info, infoLen, pos, 2);
            if (tempsW < 0) { lastError_ = "Truncated temp warning count"; warnOk = false; }
            for (int t = 0; warnOk && t < tempsW; t++) {
                if (pos + 2 > infoLen) { lastError_ = "Truncated temp warning data"; warnOk = false; break; }
                const char* text = warningStateText(info + pos);
                if (text) { warn.warnings += "temp " + String(t + 1) + " " + text + ", "; }
                pos += 2;
            }
        }

        if (warnOk && pos + 14 > infoLen) { lastError_ = "Truncated warning info"; warnOk = false; }

        if (warnOk) {
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

            warn.warnings.trim();
            while (warn.warnings.endsWith(",")) {
                warn.warnings.remove(warn.warnings.length() - 1);
                warn.warnings.trim();
            }
        }
    } else {
        warnOk = false;
    }

    if (!warnOk) {
        // Warn-info failures are reported but must not fail the whole pack - only the analog
        // reading (voltage/current/SOC/cells/temps) determines success/debounce-zeroing, since the
        // warn query has historically been flakier than the analog one and the two are otherwise
        // unrelated.
        lastError_ = "Warn: " + lastError_;
    }
    if (!ok) {
        lastError_ = "Analog: " + analogError + (warnOk ? "" : (" | " + lastError_));
        return false;
    }
    return true;
}

bool PaceRs485AsciiClient::poll(PaceBmsSnapshot& snapshot) {
    uint16_t mask = RuntimeSettings::modbusPackAddressMask();
    uint8_t index = 0;
    String cycleErrors;

    // The doc only defines COMMAND 01H-0FH, but address 0 is included here too - the previous
    // master pack was just moved from address 1 to address 0 and is now an ordinary slave, worth
    // testing empirically rather than assuming the documented gap holds in practice.
    for (uint8_t addr = 0; addr <= 15 && index < PACE_MAX_PACKS; addr++) {
        if (!(mask & (1u << addr))) continue;

        snapshot.packAddress[index] = addr;
        if (pollPack(addr, snapshot.packs[index], snapshot.warn[index])) {
            failCount_[index] = 0;
        } else {
            if (cycleErrors.length() > 0) cycleErrors += "; ";
            cycleErrors += "Adresse " + String(addr) + ": " + lastError_;
            Serial.printf("RS485-ASCII: Pack Adresse %u antwortet nicht (%s)\n", addr,
                          lastError_.c_str());
            if (failCount_[index] < 255) failCount_[index]++;
            if (failCount_[index] >= BMS_ZERO_AFTER_CONSECUTIVE_FAILURES) {
                snapshot.packs[index] = PacePackAnalog();
                snapshot.warn[index] = PacePackWarn();
            }
        }
        index++;
    }

    lastError_ = cycleErrors;
    snapshot.packCount = index;
    if (index == 0) {
        lastError_ = "Keine RS485-Packadressen konfiguriert";
        return false;
    }

    uint32_t remain = 0, full = 0, design = 0;
    for (uint8_t i = 0; i < index; i++) {
        remain += snapshot.packs[i].remainingCapacityMah;
        full += snapshot.packs[i].fullCapacityMah;
        design += snapshot.packs[i].designCapacityMah;
    }
    snapshot.capacity.remainCapacityMah = remain;
    snapshot.capacity.fullCapacityMah = full;
    snapshot.capacity.designCapacityMah = design;
    snapshot.capacity.socPercent = full > 0 ? (remain * 100.0f) / full : 0;
    snapshot.capacity.sohPercent = design > 0 ? (full * 100.0f) / design : 0;

    if (snapshot.bmsVersion.length() == 0) snapshot.bmsVersion = "RS485 (ASCII)";

    snapshot.valid = true;
    snapshot.lastUpdateMs = millis();
    return true;
}
