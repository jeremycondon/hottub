#include "../spa_control.h"
#include "../balboa_frame.h"
#include <cassert>
#include <cstdio>

// Wrap a body in a full CRC'd frame for feeding onFrame().
static size_t frame(uint8_t* out, const uint8_t* body, size_t n) {
    return balboa_build_frame(out, 64, body, n);
}

int main() {
    SpaProtocol p;
    uint8_t buf[64];

    // Status frames are parsed regardless of arm state.
    const uint8_t status[] = {0xFF,0xAF,0x13,
        0x00,0x03,0x5F,0x0C,0x29,0x00,0x28,0x55,0x00,0x00,0x04,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x02,0x02,0x55,0x00,0x00,0x02,0x3C,0x00,0x00};
    size_t n = frame(buf, status, sizeof status);
    p.onFrame(buf, n, 1000);
    assert(p.state().currentTempF == 95 && p.state().setTempF == 85);

    // Command queue accepts commands.
    assert(!p.hasPendingCommand());
    p.cmdSetTemp(102);
    assert(p.hasPendingCommand());

    printf("ALL TESTS PASSED\n");
    return 0;
}
