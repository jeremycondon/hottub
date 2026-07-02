#include "spa_reconcile.h"

void SpaReconciler::setTemp(uint8_t t)  { temp_  = Goal{true, t, 0, false, 0, 0}; tempGaveUp_  = false; }
void SpaReconciler::setPump1(uint8_t l) { pump1_ = Goal{true, l, 0, false, 0, 0}; pump1GaveUp_ = false; }
void SpaReconciler::setPump2(bool on)   { pump2_ = Goal{true, (uint8_t)(on ? 1 : 0), 0, false, 0, 0}; pump2GaveUp_ = false; }
void SpaReconciler::setLight(bool on)   { light_ = Goal{true, (uint8_t)(on ? 1 : 0), 0, false, 0, 0}; lightGaveUp_ = false; }
void SpaReconciler::clearGaveUp()       { tempGaveUp_ = pump1GaveUp_ = pump2GaveUp_ = lightGaveUp_ = false; }

// Advance one goal against the current actuator value; returns what to do.
SpaReconciler::Act SpaReconciler::step(Goal& g, uint8_t cur, uint32_t nowMs) {
    if (!g.active) return Act::None;
    if (cur == g.target) { g.active = false; g.attempts = 0; g.waiting = false; return Act::None; }
    if (g.waiting) {
        if (cur != g.observedAtIssue)                 g.waiting = false;   // effect / panel change
        else if (nowMs - g.issuedMs > EFFECT_TIMEOUT_MS) g.waiting = false; // command lost
        else return Act::None;                                            // still waiting
    }
    if (g.attempts < MAX_ATTEMPTS) {
        g.attempts++; g.waiting = true; g.issuedMs = nowMs; g.observedAtIssue = cur;
        return Act::Issue;
    }
    g.active = false;                                                     // exhausted
    return Act::GaveUp;
}

void SpaReconciler::tick(const SpaState& s, SpaProtocol& proto, uint32_t nowMs) {
    switch (step(temp_, s.setTempF, nowMs)) {
        case Act::Issue:  proto.cmdSetTemp(temp_.target); break;
        case Act::GaveUp: tempGaveUp_ = true; break;
        default: break;
    }
    switch (step(pump1_, s.pump1, nowMs)) {
        case Act::Issue:  proto.cmdTogglePump1(); break;
        case Act::GaveUp: pump1GaveUp_ = true; break;
        default: break;
    }
    switch (step(pump2_, s.pump2 ? 1 : 0, nowMs)) {
        case Act::Issue:  proto.cmdTogglePump2(); break;
        case Act::GaveUp: pump2GaveUp_ = true; break;
        default: break;
    }
    switch (step(light_, s.light ? 1 : 0, nowMs)) {
        case Act::Issue:  proto.cmdToggleLight(); break;
        case Act::GaveUp: lightGaveUp_ = true; break;
        default: break;
    }
}
