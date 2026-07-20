#pragma once
#include <Arduino.h>
#include "BmsData.h"

// Queries individual packs directly over the RS485 bus using PACE's RS232 ASCII protocol (same
// framing as PaceBmsClient/PaceBmsProtocol) instead of Modbus RTU. Confirmed via a passive bus
// sniff that the master itself polls its slave packs this way internally (CID1=46H, CID2=42H/44H,
// with ADR set to the target pack's real address and a matching COMMAND byte) - this class
// replicates that same exchange from our side, uniformly for every configured address including
// the master's own (ADR=01/COMMAND=01 is expected to work the same way as any other address).
class PaceRs485AsciiClient {
public:
    PaceRs485AsciiClient(HardwareSerial& serial, int dePin, unsigned long responseTimeoutMs = 500);

    // Sends both the analog-info and warn-info queries for one specific pack address (1-15) and
    // fills pack/warn in place. Returns true only if both succeeded.
    bool pollPack(uint8_t targetAddr, PacePackAnalog& pack, PacePackWarn& warn);

    // Same address-mask-driven loop as PaceModbusClient::poll(), reusing
    // RuntimeSettings::modbusPackAddressMask() - lets NetworkTask swap transports with a one-line
    // change instead of rewriting its polling loop.
    bool poll(PaceBmsSnapshot& snapshot);

    const String& lastError() const { return lastError_; }

private:
    HardwareSerial& serial_;
    int dePin_;
    unsigned long responseTimeoutMs_;
    String lastError_;
    uint8_t failCount_[PACE_MAX_PACKS] = {0};

    bool sendAndReceive(uint8_t targetAddr, const char* cid2, const uint8_t*& info,
                        size_t& infoLen);
    bool readFrame(uint8_t* buf, size_t cap, size_t& outLen);

    static constexpr size_t kResponseBufCap = 256;
    uint8_t responseBuf_[kResponseBufCap];
};
