#pragma once
#include <stdint.h>
#include "spa_state.h"
#include "spa_control.h"

// Turns absolute desired states (from HomeKit) into bounded, effect-paced
// commands to the spa. Call tick() on each fresh status frame.
class SpaReconciler {
public:
    static const uint8_t  MAX_ATTEMPTS      = 3;
    static const uint32_t EFFECT_TIMEOUT_MS = 2000;

    void setTemp(uint8_t targetF);
    void setPump1(uint8_t level);   // 0=off, 1=low, 2=high
    void setPump2(bool on);
    void setLight(bool on);

    void tick(const SpaState& s, SpaProtocol& proto, uint32_t nowMs);

    bool tempGaveUp()  const { return tempGaveUp_; }
    bool pump1GaveUp() const { return pump1GaveUp_; }
    bool pump2GaveUp() const { return pump2GaveUp_; }
    bool lightGaveUp() const { return lightGaveUp_; }
    void clearGaveUp();

private:
    enum class Act { None, Issue, GaveUp };
    struct Goal {
        bool     active   = false;
        uint8_t  target   = 0;
        uint8_t  attempts = 0;
        bool     waiting  = false;
        uint32_t issuedMs = 0;
        uint8_t  observedAtIssue = 0;
    };
    Act step(Goal& g, uint8_t cur, uint32_t nowMs);

    Goal temp_, pump1_, pump2_, light_;
    bool tempGaveUp_ = false, pump1GaveUp_ = false, pump2GaveUp_ = false, lightGaveUp_ = false;
};
