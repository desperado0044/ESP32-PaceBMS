#include "ModbusRtuProtocol.h"

namespace ModbusRtuProtocol {

namespace {
constexpr uint8_t kFuncReadHoldingRegisters = 0x03;
constexpr uint8_t kExceptionBit = 0x80;
}  // namespace

uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

size_t buildReadHoldingRegistersRequest(uint8_t slaveAddress, uint16_t startRegister,
                                         uint16_t registerCount, uint8_t* outFrame,
                                         size_t outFrameCapacity) {
    constexpr size_t kBodyLen = 6;
    constexpr size_t kTotalLen = kBodyLen + 2;  // + CRC16
    if (outFrameCapacity < kTotalLen) return 0;

    outFrame[0] = slaveAddress;
    outFrame[1] = kFuncReadHoldingRegisters;
    outFrame[2] = (uint8_t)(startRegister >> 8);
    outFrame[3] = (uint8_t)(startRegister & 0xFF);
    outFrame[4] = (uint8_t)(registerCount >> 8);
    outFrame[5] = (uint8_t)(registerCount & 0xFF);

    uint16_t crc = crc16(outFrame, kBodyLen);
    outFrame[6] = (uint8_t)(crc & 0xFF);  // CRC is sent low byte first
    outFrame[7] = (uint8_t)(crc >> 8);

    return kTotalLen;
}

ParsedResponse parseReadHoldingRegistersResponse(const uint8_t* frame, size_t len,
                                                  uint8_t expectedAddress) {
    ParsedResponse r;

    if (len < 5) {
        r.error = "Frame too short";
        return r;
    }
    if (frame[0] != expectedAddress) {
        r.error = "Unexpected slave address in response";
        return r;
    }

    uint16_t receivedCrc = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
    uint16_t calcCrc = crc16(frame, len - 2);
    if (receivedCrc != calcCrc) {
        r.error = "CRC16 mismatch";
        return r;
    }

    if (frame[1] == (kFuncReadHoldingRegisters | kExceptionBit)) {
        r.error = "Modbus exception response";
        return r;
    }
    if (frame[1] != kFuncReadHoldingRegisters) {
        r.error = "Unexpected function code in response";
        return r;
    }

    uint8_t byteCount = frame[2];
    if (len < (size_t)(3 + byteCount + 2) || byteCount % 2 != 0) {
        r.error = "Malformed byte count in response";
        return r;
    }

    r.ok = true;
    r.registerData = frame + 3;
    r.registerCount = byteCount / 2;
    return r;
}

uint16_t registerU16(const uint8_t* registerData, size_t index) {
    return ((uint16_t)registerData[index * 2] << 8) | registerData[index * 2 + 1];
}

int16_t registerI16(const uint8_t* registerData, size_t index) {
    return (int16_t)registerU16(registerData, index);
}

}  // namespace ModbusRtuProtocol
