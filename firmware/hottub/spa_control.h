#pragma once
#include <stdint.h>
#include <stddef.h>
#include "spa_state.h"

class SpaProtocol {
public:
    enum RegState { Unregistered, Requesting, Assigned };

    void setArmed(bool a);
    bool armed() const { return armed_; }
    RegState regState() const { return reg_; }
    uint8_t channel() const { return channel_; }
    const SpaState& state() const { return state_; }
    bool hasPendingCommand() const { return qCount_ > 0; }
    size_t pendingCount() const { return qCount_; }

    void cmdSetTemp(uint8_t tempF);
    void cmdTogglePump1();
    void cmdTogglePump2();
    void cmdToggleLight();

    void onFrame(const uint8_t* frame, size_t len, uint32_t nowMs);
    size_t pollTx(uint8_t* out, size_t outCap);

private:
    enum Owe { OweNone, OweRequest, OweAck, OweCommand, OweNothing };
    struct Cmd { uint8_t type; uint8_t arg; };   // type 0x20=set temp, 0x11=toggle

    static const size_t QCAP = 8;
    bool     armed_   = false;
    RegState reg_     = Unregistered;
    uint8_t  channel_ = 0;
    Owe      owe_     = OweNone;
    SpaState state_;
    Cmd      queue_[QCAP];
    size_t   qHead_ = 0, qCount_ = 0;

    void enqueue(uint8_t type, uint8_t arg);
    size_t encodeCommand(const Cmd& c, uint8_t* out, size_t cap);
};
