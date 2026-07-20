#pragma once
#include <Arduino.h>
#include "BmsData.h"

// High-level PACE Modbus RTU client (RS485), an alternative to PaceBmsClient (RS232/ASCII) - reads
// the same kind of data into the same PaceBmsSnapshot/BmsData.h structs, so the rest of the
// firmware (display/web/MQTT) doesn't need to know or care which transport is active. Uses the
// register map from PACE-BMS-Modbus-Protocol-for-RS485-V1.3-20170627.pdf (registers 0-36 cover
// everything but the version/serial number strings). Modbus has no "give me all packs" query like
// RS232's CID2 0x42 - instead each physical pack is its own bus slave (dip-switch address 0-15), so
// multi-pack support here means polling whichever addresses RuntimeSettings::modbusPackAddressMask()
// says are installed, one at a time, each with its own debounced zero-on-disconnect handling.
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
    // Indexed by position in the ascending list of configured addresses (stable for this client's
    // lifetime - the address selection only takes effect after a reboot, same as other runtime
    // settings), not by the address value itself.
    uint8_t failCount_[PACE_MAX_PACKS] = {0};

    bool readRegisters(uint8_t slaveAddress, uint16_t startRegister, uint16_t count,
                        const uint8_t*& registerData, size_t& registerCount);
    bool readFrame(uint8_t* buf, size_t cap, size_t& outLen);
    void fillPackFromRegisters(const uint8_t* regs, PacePackAnalog& pack, PacePackWarn& warn);

    static constexpr size_t kResponseBufCap = 96;
    uint8_t responseBuf_[kResponseBufCap];
};
