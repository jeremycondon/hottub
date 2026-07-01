// Host-side tests for Balboa frame parsing. Build & run:
//   g++ -std=c++17 -Wall -o /tmp/tbf test/test_balboa_frame.cpp && /tmp/tbf
//
// This file is in test/ so arduino-cli does not compile it into the firmware.
#include "../balboa_frame.h"
#include <cassert>
#include <cstdio>

// A real status-broadcast frame captured from a Balboa BP501 bus, with the
// 0x7E start flag and LEN byte restored (the sniffer had been dropping them).
static const uint8_t kGoodFrame[] = {
    0x7E, 0x20, 0xFF, 0xAF, 0x13, 0x00, 0x01, 0xFF, 0x0C, 0x00, 0x00, 0x13,
    0x01, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x56, 0x00, 0x00, 0x02, 0x1E, 0x00, 0x00, 0x57, 0x7E,
};
static const size_t kGoodLen = sizeof(kGoodFrame);

int main() {
    // CRC is computed over LEN..last-data = frame[1] .. frame[len-3].
    uint8_t crc = balboa_crc8(kGoodFrame + 1, kGoodLen - 3);
    printf("balboa_crc8 = 0x%02X (want 0x57)\n", crc);
    assert(crc == 0x57);

    // A real, complete frame validates.
    assert(balboa_frame_valid(kGoodFrame, kGoodLen) == true);

    // A single corrupted payload byte fails CRC.
    uint8_t bad[kGoodLen];
    for (size_t i = 0; i < kGoodLen; i++) bad[i] = kGoodFrame[i];
    bad[10] ^= 0xFF;
    assert(balboa_frame_valid(bad, kGoodLen) == false);

    // Wrong flags / length are rejected.
    uint8_t noFlag[kGoodLen];
    for (size_t i = 0; i < kGoodLen; i++) noFlag[i] = kGoodFrame[i];
    noFlag[0] = 0x00;                       // missing start flag
    assert(balboa_frame_valid(noFlag, kGoodLen) == false);

    uint8_t badLen[kGoodLen];
    for (size_t i = 0; i < kGoodLen; i++) badLen[i] = kGoodFrame[i];
    badLen[1] = 0x19;                       // LEN doesn't match actual size
    assert(balboa_frame_valid(badLen, kGoodLen) == false);

    // Streaming reader extracts two back-to-back frames that SHARE the 0x7E
    // flag between them (frame2 reuses frame1's trailing flag as its start).
    uint8_t stream[kGoodLen + (kGoodLen - 1)];
    size_t n = 0;
    for (size_t i = 0; i < kGoodLen; i++) stream[n++] = kGoodFrame[i];   // frame1 + flag
    for (size_t i = 1; i < kGoodLen; i++) stream[n++] = kGoodFrame[i];   // frame2 (skip dup flag)

    BalboaFrameReader r;
    int complete = 0;
    for (size_t i = 0; i < n; i++) {
        if (r.push(stream[i])) {
            assert(balboa_frame_valid(r.buf, r.len));
            complete++;
        }
    }
    printf("reader extracted %d frames (want 2)\n", complete);
    assert(complete == 2);

    printf("ALL TESTS PASSED\n");
    return 0;
}
