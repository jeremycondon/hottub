#include "../balboa_frame.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    // Build the real new-client CTS frame and match the captured bytes.
    const uint8_t body[] = {0xFE, 0xBF, 0x00};
    const uint8_t want[] = {0x7E, 0x05, 0xFE, 0xBF, 0x00, 0xAC, 0x7E};
    uint8_t out[16];
    size_t n = balboa_build_frame(out, sizeof out, body, sizeof body);
    assert(n == sizeof want);
    assert(memcmp(out, want, n) == 0);

    // A built frame must pass our own validator and reader.
    assert(balboa_frame_valid(out, n));
    BalboaFrameReader r; int done = 0;
    for (size_t i = 0; i < n; i++) if (r.push(out[i])) done++;
    assert(done == 1);

    // Capacity guard.
    assert(balboa_build_frame(out, 4, body, sizeof body) == 0);

    printf("ALL TESTS PASSED\n");
    return 0;
}
