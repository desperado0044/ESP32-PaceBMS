#pragma once
#include <Arduino.h>

// Low-level framing for the PACE BMS RS232 ASCII-hex protocol.
// Frame layout: SOI(~) VER(2) ADR(2) CID1(2) CID2(2) LCHKSUM(1) LENID(3) INFO(LENID) CHKSUM(4) EOI(\r)
// All fields except SOI/EOI are ASCII hex characters. Ported from github.com/Tertiush/bmspace.

namespace PaceBmsProtocol {

constexpr char SOI = '~';
constexpr char EOI = '\r';

// Builds a full request frame into outFrame. infoAscii is the already hex-encoded
// ASCII payload (e.g. "FF"), or nullptr/"" for no payload. Returns the frame length,
// or 0 if outFrame is too small.
size_t buildRequest(const char* ver, const char* adr, const char* cid1, const char* cid2,
                     const char* infoAscii, uint8_t* outFrame, size_t outFrameCapacity);

struct ParsedResponse {
    bool ok = false;
    uint8_t rtn = 0;
    const uint8_t* info = nullptr;  // points into the input frame buffer
    size_t infoLen = 0;
    const char* error = nullptr;
};

// Validates SOI, RTN, LCHKSUM and CHKSUM of a received frame (frame[0] must be SOI,
// frame[len-1] should be EOI though it is not required for validation).
ParsedResponse parseResponse(const uint8_t* frame, size_t len);

// Reads nChars ASCII-hex digits at data[pos] and returns them as an unsigned value.
// Advances pos by nChars. Returns -1 if not enough data remains.
long readHexField(const uint8_t* data, size_t len, size_t& pos, size_t nChars);

}  // namespace PaceBmsProtocol
