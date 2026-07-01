#include "../spa_state.h"
#include <cassert>
#include <cstdio>

// Payloads = bytes AFTER FF AF 13, taken from real BP501 captures.
// 95F cur / 85F set, everything off, high range:
static const uint8_t idle[] = {
    0x00,0x03,0x5F,0x0C,0x29,0x00,0x28,0x55,0x00,0x00,0x04,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x02,0x02,0x55,0x00,0x00,0x02,0x3C,0x00,0x00};
// cur unknown (0xFF) / 84F set, pump1 high + pump2 on, light on:
static const uint8_t active[] = {
    0x00,0x03,0xFF,0x01,0x1E,0x00,0x28,0x54,0x00,0x00,0x0C,0x0A,0x00,0x00,
    0x00,0x00,0x00,0x00,0x02,0x02,0x54,0x00,0x00,0x02,0x1E,0x00,0x00};

int main() {
    SpaState s;
    assert(parseStatus(idle, sizeof idle, s, 1000));
    assert(s.tempKnown && s.currentTempF == 95);
    assert(s.setTempF == 85);
    assert(s.pump1 == 0 && !s.pump2 && !s.light && !s.heating && s.highRange && !s.circ);
    assert(!s.stale && s.lastUpdateMs == 1000);

    assert(parseStatus(active, sizeof active, s, 2000));
    assert(!s.tempKnown && s.currentTempF == 95);   // holds last known
    assert(s.setTempF == 84);
    assert(s.pump1 == 2 && s.pump2 && s.light && s.highRange);

    assert(!parseStatus(idle, 10, s, 3000));         // too short -> false

    printf("ALL TESTS PASSED\n");
    return 0;
}
