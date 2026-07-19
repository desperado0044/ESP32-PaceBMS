#pragma once
#include <Arduino.h>

// Minimal Modbus RTU framing for function 0x03 (Read Holding Registers) - all the PACE Modbus
// register map needs. Pure binary protocol (unlike PaceBmsProtocol.h's ASCII-hex framing) - CRC16
// is the standard reflected CRC-16/MODBUS (poly 0xA001, seed 0xFFFF, transmitted low-byte first).
namespace ModbusRtuProtocol {

uint16_t crc16(const uint8_t* data, size_t len);

// Builds a "Read Holding Registers" request into outFrame. Returns the frame length, or 0 if
// outFrame is too small.
size_t buildReadHoldingRegistersRequest(uint8_t slaveAddress, uint16_t startRegister,
                                         uint16_t registerCount, uint8_t* outFrame,
                                         size_t outFrameCapacity);

struct ParsedResponse {
    bool ok = false;
    const uint8_t* registerData = nullptr;  // big-endian uint16 per register, points into frame
    size_t registerCount = 0;
    const char* error = nullptr;
};

// Validates address, function code (including a Modbus exception response), byte count and CRC16.
ParsedResponse parseReadHoldingRegistersResponse(const uint8_t* frame, size_t len,
                                                  uint8_t expectedAddress);

// Reads register i (0-based within registerData) as a big-endian uint16/int16.
uint16_t registerU16(const uint8_t* registerData, size_t index);
int16_t registerI16(const uint8_t* registerData, size_t index);

}  // namespace ModbusRtuProtocol
