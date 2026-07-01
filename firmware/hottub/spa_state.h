#pragma once
#include <stdint.h>
#include <stddef.h>

struct SpaState {
    uint8_t  currentTempF = 0;   // valid only if tempKnown
    uint8_t  setTempF     = 0;
    uint8_t  pump1        = 0;   // 0=off, 1=low, 2=high
    bool     pump2        = false;
    bool     light        = false;
    bool     circ         = false;
    bool     heating      = false;
    bool     highRange    = false;
    bool     tempKnown    = false;
    uint32_t lastUpdateMs = 0;
    bool     stale        = true;
};

// payload = bytes AFTER the FF AF 13 message type. Offsets verified for BP501
// (see docs/balboa-protocol.md). Returns false if the payload is too short.
inline bool parseStatus(const uint8_t* p, size_t len, SpaState& s, uint32_t nowMs) {
    if (len < 21) return false;
    if (p[2] != 0xFF) { s.currentTempF = p[2]; s.tempKnown = true; }
    else               { s.tempKnown = false; }   // hold last currentTempF
    s.setTempF   = p[20];
    s.pump1      = p[11] & 0x03;
    s.pump2      = ((p[11] >> 2) & 0x03) != 0;
    s.light      = p[10] & 0x08;
    s.highRange  = p[10] & 0x04;
    s.heating    = p[10] & 0x20;
    s.circ       = p[13] & 0x02;
    s.lastUpdateMs = nowMs;
    s.stale = false;
    return true;
}
