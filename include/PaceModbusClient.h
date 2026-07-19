#pragma once
#include <Arduino.h>
#include "BmsData.h"

// High-level PACE Modbus RTU client (RS485), an alternative to PaceBmsClient (RS232/ASCII) - reads
// the same kind of data into the same PaceBmsSnapshot/BmsData.h structs, so the rest of the
// firmware (display/web/MQTT) doesn't need to know or care which transport is active. Uses the
// register map from PACE-BMS-Modbus-Protocol-for-RS485-V1.3-20170627.pdf (registers 0-36 cover
// everything but the version/serial number strings). Single-pack only - the Modbus register map
// doesn't expose a multi-pack concept the way the RS232 protocol's CID2 0x42 "all packs" query
// does; snapshot.packCount is always set to (at most) 1.
class PaceModbusClient {
public:
    PaceModbusClient(HardwareSerial& serial, int dePin, unsigned long responseTimeoutMs = 500);

    bool poll(PaceBmsSnapshot& snapshot);

    const String& lastError() const { return lastError_; }

private:
    HardwareSerial& serial_;
    int dePin_;
    unsigned long responseTimeoutMs_;
    String lastError_;

    bool readRegisters(uint16_t startRegister, uint16_t count, const uint8_t*& registerData,
                        size_t& registerCount);
    bool readFrame(uint8_t* buf, size_t cap, size_t& outLen);

    static constexpr size_t kResponseBufCap = 96;
    uint8_t responseBuf_[kResponseBufCap];
};
