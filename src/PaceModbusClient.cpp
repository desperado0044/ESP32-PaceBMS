#include "PaceModbusClient.h"
#include "ModbusRtuProtocol.h"
#include "Config.h"

namespace {

struct FlagLabel {
    uint8_t bit;
    const char* text;
};

// Bit layouts from PACE-BMS-Modbus-Protocol-for-RS485-V1.3-20170627.pdf. Reserved bits are simply
// omitted here rather than listed - nothing to report for them either way.
constexpr FlagLabel kWarningLabels[] = {
    {0, "Zelle Ueberspannung"},        {1, "Zelle Unterspannung"},
    {2, "Pack Ueberspannung"},         {3, "Pack Unterspannung"},
    {4, "Ladestrom hoch"},             {5, "Entladestrom hoch"},
    {8, "Ladetemperatur hoch"},        {9, "Entladetemperatur hoch"},
    {10, "Ladetemperatur niedrig"},    {11, "Entladetemperatur niedrig"},
    {12, "Umgebungstemperatur hoch"},  {13, "Umgebungstemperatur niedrig"},
    {14, "MOSFET-Temperatur hoch"},    {15, "SOC niedrig"},
};

constexpr FlagLabel kProtectionLabels[] = {
    {0, "Zelle Ueberspannung (Schutz)"},      {1, "Zelle Unterspannung (Schutz)"},
    {2, "Pack Ueberspannung (Schutz)"},       {3, "Pack Unterspannung (Schutz)"},
    {4, "Ladestrom (Schutz)"},                {5, "Entladestrom (Schutz)"},
    {6, "Kurzschluss"},                       {7, "Laderspannung ueber Grenzwert"},
    {8, "Ladetemperatur hoch (Schutz)"},      {9, "Entladetemperatur hoch (Schutz)"},
    {10, "Ladetemperatur niedrig (Schutz)"},  {11, "Entladetemperatur niedrig (Schutz)"},
    {12, "MOSFET-Temperatur hoch (Schutz)"},  {13, "Umgebungstemperatur hoch (Schutz)"},
    {14, "Umgebungstemperatur niedrig (Schutz)"},
};

// Only the actual fault bits - bits 8-15 of this register are ON/OFF status indicators (FET
// state, charging limiter, heater, ...), handled separately in poll(), not warnings.
constexpr FlagLabel kFaultLabels[] = {
    {0, "Lade-MOSFET defekt"},   {1, "Entlade-MOSFET defekt"},        {2, "Temperatursensor defekt"},
    {4, "Zellfehler"},           {5, "Abtast-Kommunikationsfehler"},
};

void appendSetBits(String& out, uint16_t flags, const FlagLabel* labels, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (flags & (1u << labels[i].bit)) {
            if (out.length() > 0) out += ", ";
            out += labels[i].text;
        }
    }
}

String buildWarningsText(uint16_t warningFlag, uint16_t protectionFlag, uint16_t statusFlag) {
    String out;
    appendSetBits(out, warningFlag, kWarningLabels, sizeof(kWarningLabels) / sizeof(kWarningLabels[0]));
    appendSetBits(out, protectionFlag, kProtectionLabels,
                  sizeof(kProtectionLabels) / sizeof(kProtectionLabels[0]));
    appendSetBits(out, statusFlag, kFaultLabels, sizeof(kFaultLabels) / sizeof(kFaultLabels[0]));
    return out;
}

}  // namespace

PaceModbusClient::PaceModbusClient(HardwareSerial& serial, int dePin, unsigned long responseTimeoutMs)
    : serial_(serial), dePin_(dePin), responseTimeoutMs_(responseTimeoutMs) {}

bool PaceModbusClient::readFrame(uint8_t* buf, size_t cap, size_t& outLen) {
    unsigned long start = millis();
    size_t len = 0;
    unsigned long lastByteMs = 0;

    while (millis() - start < responseTimeoutMs_) {
        if (serial_.available()) {
            if (len >= cap) {
                lastError_ = "Modbus response exceeds buffer size";
                return false;
            }
            buf[len++] = (uint8_t)serial_.read();
            lastByteMs = millis();
        } else if (len > 0 && millis() - lastByteMs > 10) {
            // Quiet gap after at least one byte - standard Modbus RTU end-of-frame signal (no
            // explicit terminator like the RS232 protocol's EOI byte).
            outLen = len;
            return true;
        }
    }

    if (len > 0) {
        outLen = len;
        return true;  // let the caller's CRC check catch a genuinely truncated frame
    }
    lastError_ = "Timeout waiting for Modbus response";
    return false;
}

bool PaceModbusClient::readRegisters(uint16_t startRegister, uint16_t count,
                                      const uint8_t*& registerData, size_t& registerCount) {
    uint8_t request[8];
    size_t requestLen = ModbusRtuProtocol::buildReadHoldingRegistersRequest(
        MODBUS_SLAVE_ADDRESS, startRegister, count, request, sizeof(request));
    if (requestLen == 0) {
        lastError_ = "Failed to build Modbus request";
        return false;
    }

    while (serial_.available()) serial_.read();  // drop stale bytes before requesting

    digitalWrite(dePin_, HIGH);  // enable RS485 driver (transmit)
    serial_.write(request, requestLen);
    serial_.flush();             // block until the request has actually gone out the wire
    digitalWrite(dePin_, LOW);   // back to receive

    size_t responseLen = 0;
    if (!readFrame(responseBuf_, kResponseBufCap, responseLen)) return false;

    ModbusRtuProtocol::ParsedResponse resp =
        ModbusRtuProtocol::parseReadHoldingRegistersResponse(responseBuf_, responseLen,
                                                              MODBUS_SLAVE_ADDRESS);
    if (!resp.ok) {
        lastError_ = resp.error ? resp.error : "Unknown Modbus error";
        return false;
    }

    registerData = resp.registerData;
    registerCount = resp.registerCount;
    return true;
}

bool PaceModbusClient::poll(PaceBmsSnapshot& snapshot) {
    using namespace ModbusRtuProtocol;

    const uint8_t* regs;
    size_t regCount;
    // Registers 0-36 cover current/voltage/SOC/SOH/capacities/cycles/warning-protection-status/
    // balance/cell voltages/temperatures in a single request - registers 8, 13-14 are reserved
    // gaps but Modbus happily returns whatever's there for those, no need to split the request.
    if (!readRegisters(0, 37, regs, regCount)) return false;
    if (regCount < 37) {
        lastError_ = "Truncated Modbus register block";
        return false;
    }

    snapshot.packCount = 1;  // the Modbus register map has no multi-pack concept, unlike RS232's
                              // CID2 0x42 "all packs" query
    PacePackAnalog& pack = snapshot.packs[0];
    PacePackWarn& warn = snapshot.warn[0];

    pack.packCurrentA = registerI16(regs, 0) / 100.0f;    // 10mA units, + charging / - discharging
    pack.packVoltageV = registerU16(regs, 1) / 100.0f;    // 10mV units
    pack.socPercent = registerU16(regs, 2);                // low byte only meaningful, 0-100
    pack.sohPercent = registerU16(regs, 3);
    pack.remainingCapacityMah = (uint32_t)registerU16(regs, 4) * 10;  // 10mAh units
    pack.fullCapacityMah = (uint32_t)registerU16(regs, 5) * 10;
    pack.designCapacityMah = (uint32_t)registerU16(regs, 6) * 10;
    pack.cycles = registerU16(regs, 7);

    uint16_t warningFlag = registerU16(regs, 9);
    uint16_t protectionFlag = registerU16(regs, 10);
    uint16_t statusFlag = registerU16(regs, 11);
    uint16_t balanceStatus = registerU16(regs, 12);

    pack.cellCount = PACE_MAX_CELLS < 16 ? PACE_MAX_CELLS : 16;
    uint16_t minMv = 0xFFFF, maxMv = 0;
    for (uint8_t i = 0; i < pack.cellCount; i++) {
        uint16_t mv = registerU16(regs, 15 + i);
        pack.cellMillivolts[i] = mv;
        if (mv < minMv) minMv = mv;
        if (mv > maxMv) maxMv = mv;
    }
    pack.cellMaxDiffMv = maxMv - minMv;

    pack.tempCount = 6;  // 4 cell temperature sensors + MOSFET + environment
    for (int i = 0; i < 4; i++) pack.temperaturesC[i] = registerI16(regs, 31 + i) / 10.0f;
    pack.temperaturesC[4] = registerI16(regs, 35) / 10.0f;  // MOSFET
    pack.temperaturesC[5] = registerI16(regs, 36) / 10.0f;  // environment

    warn.balanceState1 = (uint8_t)(balanceStatus & 0xFF);
    warn.balanceState2 = (uint8_t)(balanceStatus >> 8);
    warn.protShortCircuit = (protectionFlag >> 6) & 1;
    warn.protDischargeCurrent = (protectionFlag >> 5) & 1;
    warn.protChargeCurrent = (protectionFlag >> 4) & 1;
    warn.fullyCharged = false;      // no equivalent register in the Modbus map
    warn.currentLimitOn = (statusFlag >> 12) & 1;
    warn.chargeFetOn = (statusFlag >> 10) & 1;
    warn.dischargeFetOn = (statusFlag >> 11) & 1;
    warn.packIndicateOn = false;    // no equivalent register in the Modbus map
    warn.reverseOn = (statusFlag >> 14) & 1;
    warn.acInOn = false;            // no equivalent register in the Modbus map
    warn.heartOn = (statusFlag >> 15) & 1;  // "heater" bit - closest match to the RS232 field
    warn.warnings = buildWarningsText(warningFlag, protectionFlag, statusFlag);

    snapshot.capacity.remainCapacityMah = pack.remainingCapacityMah;
    snapshot.capacity.fullCapacityMah = pack.fullCapacityMah;
    snapshot.capacity.designCapacityMah = pack.designCapacityMah;
    snapshot.capacity.socPercent = pack.socPercent;
    snapshot.capacity.sohPercent = pack.sohPercent;

    if (snapshot.bmsVersion.length() == 0) snapshot.bmsVersion = "Modbus";
    // Version/serial-number registers (150+) intentionally not read - not essential for
    // monitoring, and the RS232 client already covers that path when Modbus isn't selected.

    snapshot.valid = true;
    snapshot.lastUpdateMs = millis();
    return true;
}
