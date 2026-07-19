#include "PaceBmsProtocol.h"

namespace PaceBmsProtocol {

static int hexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Sum of the ASCII byte values of body[1..len-1] (the SOI at index 0 is excluded),
// folded into a 16-bit two's-complement checksum, formatted as 4 uppercase hex chars.
static void computeChksum(const uint8_t* body, size_t len, char out[4]) {
    uint32_t sum = 0;
    for (size_t i = 1; i < len; i++) sum += body[i];
    uint16_t chksum = (uint16_t)(0x10000u - (sum % 0x10000u));
    char buf[5];
    snprintf(buf, sizeof(buf), "%04X", chksum);
    memcpy(out, buf, 4);
}

// Sum of the hex-nibble values of the 3 LENID chars, folded into a 4-bit
// two's-complement checksum, formatted as a single uppercase hex char.
static char computeLchksum(const char lenid[3]) {
    int sum = 0;
    for (int i = 0; i < 3; i++) sum += hexDigitValue(lenid[i]);
    int lchksum = (16 - (sum % 16)) % 16;
    return "0123456789ABCDEF"[lchksum];
}

size_t buildRequest(const char* ver, const char* adr, const char* cid1, const char* cid2,
                     const char* infoAscii, uint8_t* outFrame, size_t outFrameCapacity) {
    size_t infoLen = infoAscii ? strlen(infoAscii) : 0;

    char lenid[4];
    snprintf(lenid, sizeof(lenid), "%03X", (unsigned)infoLen);

    char lchksumChar = computeLchksum(lenid);

    // Body = everything before CHKSUM/EOI: SOI VER ADR CID1 CID2 LCHKSUM LENID INFO
    size_t bodyLen = 1 + 2 + 2 + 2 + 2 + 1 + 3 + infoLen;
    size_t total = bodyLen + 4 + 1;  // + CHKSUM(4) + EOI(1)
    if (total > outFrameCapacity) return 0;

    size_t pos = 0;
    outFrame[pos++] = (uint8_t)SOI;
    memcpy(outFrame + pos, ver, 2); pos += 2;
    memcpy(outFrame + pos, adr, 2); pos += 2;
    memcpy(outFrame + pos, cid1, 2); pos += 2;
    memcpy(outFrame + pos, cid2, 2); pos += 2;
    outFrame[pos++] = (uint8_t)lchksumChar;
    memcpy(outFrame + pos, lenid, 3); pos += 3;
    if (infoLen > 0) {
        memcpy(outFrame + pos, infoAscii, infoLen);
        pos += infoLen;
    }

    char chksum[4];
    computeChksum(outFrame, bodyLen, chksum);
    memcpy(outFrame + pos, chksum, 4); pos += 4;
    outFrame[pos++] = (uint8_t)EOI;

    return pos;
}

long readHexField(const uint8_t* data, size_t len, size_t& pos, size_t nChars) {
    if (pos + nChars > len) {
        pos = len;
        return -1;
    }
    char buf[9];
    size_t n = nChars < 8 ? nChars : 8;
    memcpy(buf, data + pos, n);
    buf[n] = '\0';
    pos += nChars;
    return strtol(buf, nullptr, 16);
}

static const char* rtnErrorMessage(uint8_t rtnValue) {
    switch (rtnValue) {
        case 0x00: return nullptr;  // no error
        case 0x01: return "RTN Error 01: Undefined RTN error";
        case 0x02: return "RTN Error 02: CHKSUM error";
        case 0x03: return "RTN Error 03: LCHKSUM error";
        case 0x04: return "RTN Error 04: CID2 undefined";
        case 0x05: return "RTN Error 05: Undefined error";
        case 0x06: return "RTN Error 06: Undefined error";
        case 0x09: return "RTN Error 09: Operation or write error";
        default: return nullptr;  // matches upstream: unmapped codes are treated as not-an-error
    }
}

ParsedResponse parseResponse(const uint8_t* frame, size_t len) {
    ParsedResponse r;

    if (len < 13 || frame[0] != (uint8_t)SOI) {
        r.error = "Incorrect starting byte for incoming data";
        return r;
    }

    // RTN is at frame[7:9], two ASCII hex chars.
    size_t pos = 7;
    long rtnVal = readHexField(frame, len, pos, 2);
    if (rtnVal < 0) {
        r.error = "Frame too short to contain RTN";
        return r;
    }
    r.rtn = (uint8_t)rtnVal;
    const char* rtnMsg = rtnErrorMessage(r.rtn);
    if (rtnMsg != nullptr) {
        r.error = rtnMsg;
        return r;
    }

    char lchksumReceived = (char)frame[9];

    pos = 10;
    long lenidVal = readHexField(frame, len, pos, 3);
    if (lenidVal < 0) {
        r.error = "Frame too short to contain LENID";
        return r;
    }
    size_t lenid = (size_t)lenidVal;

    char lenidChars[3] = {(char)frame[10], (char)frame[11], (char)frame[12]};
    char calcLchksum = computeLchksum(lenidChars);
    if (lchksumReceived != calcLchksum) {
        r.error = "LCHKSUM mismatch";
        return r;
    }

    size_t infoStart = 13;
    if (infoStart + lenid + 4 > len) {
        r.error = "Frame too short to contain INFO/CHKSUM";
        return r;
    }

    const uint8_t* info = frame + infoStart;
    const uint8_t* chksumReceived = frame + infoStart + lenid;

    char calcChksum[4];
    computeChksum(frame, infoStart + lenid, calcChksum);

    if (memcmp(chksumReceived, calcChksum, 4) != 0) {
        r.error = "CHKSUM mismatch";
        return r;
    }

    r.ok = true;
    r.info = info;
    r.infoLen = lenid;
    return r;
}

}  // namespace PaceBmsProtocol
