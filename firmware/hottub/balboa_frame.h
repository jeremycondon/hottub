#pragma once
#include <stddef.h>
#include <stdint.h>

// Balboa RS485 frame layout:
//
//   0x7E <LEN> <channel> <type...> <payload...> <CRC> 0x7E
//
//   - 0x7E is the frame flag on BOTH ends.
//   - LEN  = number of bytes between the two flags, i.e. LEN itself through the
//            CRC byte (so total frame size on the wire = LEN + 2).
//   - CRC  = CRC-8, poly 0x07, init 0x02, final XOR 0x02, computed over the
//            bytes from LEN through the last payload byte (everything between
//            the flags except the CRC byte itself).
static const uint8_t BALBOA_FLAG = 0x7E;

// CRC-8 over `len` bytes: poly 0x07, init 0x02, final XOR 0x02.
inline uint8_t balboa_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x02;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
        }
    }
    return crc ^ 0x02;
}

// True if `frame` (including both 0x7E flags) is a structurally valid Balboa
// frame with a matching CRC.
inline bool balboa_frame_valid(const uint8_t* frame, size_t len) {
    // Smallest possible frame: 7E LEN CH CRC 7E
    if (len < 5) return false;
    if (frame[0] != BALBOA_FLAG || frame[len - 1] != BALBOA_FLAG) return false;
    if (frame[1] != len - 2) return false;   // LEN = bytes between the flags
    uint8_t rxCrc = frame[len - 2];
    return rxCrc == balboa_crc8(frame + 1, len - 3);
}

// Build 7E LEN <body> CRC 7E into out. body = channel + type bytes + payload.
// Returns total frame length, or 0 if it will not fit in outCap.
inline size_t balboa_build_frame(uint8_t* out, size_t outCap,
                                 const uint8_t* body, size_t bodyLen) {
    size_t total = bodyLen + 4;                 // 7E LEN <body> CRC 7E
    if (total > outCap) return 0;
    out[0] = BALBOA_FLAG;
    out[1] = (uint8_t)(bodyLen + 2);            // LEN = LEN-byte + body + CRC
    for (size_t i = 0; i < bodyLen; i++) out[2 + i] = body[i];
    out[2 + bodyLen] = balboa_crc8(out + 1, bodyLen + 1);   // CRC over LEN..last body
    out[3 + bodyLen] = BALBOA_FLAG;
    return total;
}

// Streaming frame assembler. Feed it one byte at a time with push(); it returns
// true when buf[0..len-1] holds a complete frame (validate with
// balboa_frame_valid). Length-driven so payload bytes equal to 0x7E are safe,
// and it tolerates frames that share a single 0x7E flag between them.
struct BalboaFrameReader {
    static const size_t CAP = 64;
    uint8_t buf[CAP];
    size_t  len = 0;
    size_t  expected = 0;     // full frame size once LEN is known; 0 = unknown
    bool    inFrame = false;
    bool    justCompleted = false;

    bool push(uint8_t b);     // (stub — implemented test-first)
};

inline bool BalboaFrameReader::push(uint8_t b) {
    // The trailing 0x7E of the frame we just completed is shared with the next
    // frame's start flag, so re-arm before handling this byte.
    if (justCompleted) {
        justCompleted = false;
        buf[0] = BALBOA_FLAG;
        len = 1;
        expected = 0;
        inFrame = true;
    }

    if (!inFrame) {
        if (b == BALBOA_FLAG) {
            buf[0] = b;
            len = 1;
            expected = 0;
            inFrame = true;
        }
        return false;
    }

    if (len == 1) {                       // this byte is the LEN field
        size_t full = (size_t)b + 2;      // bytes between flags + both flags
        if (b < 3 || full > CAP) {        // implausible length — resync
            if (b == BALBOA_FLAG) { buf[0] = b; len = 1; }
            else { inFrame = false; len = 0; }
            expected = 0;
            return false;
        }
        buf[len++] = b;
        expected = full;
        return false;
    }

    buf[len++] = b;                       // body byte
    if (len >= expected) {
        inFrame = false;
        if (balboa_frame_valid(buf, len) && buf[len - 1] == BALBOA_FLAG) {
            justCompleted = true;         // shared flag re-arms the next frame
            return true;
        }
        // Invalid frame: resync on the trailing byte if it's a flag.
        if (buf[len - 1] == BALBOA_FLAG) { buf[0] = BALBOA_FLAG; len = 1; inFrame = true; }
        else { len = 0; }
        expected = 0;
    }
    return false;
}
