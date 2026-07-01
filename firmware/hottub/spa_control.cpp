#include "spa_control.h"
#include "balboa_frame.h"

static const uint8_t CMD_SET_TEMP = 0x20;
static const uint8_t CMD_TOGGLE   = 0x11;
static const uint8_t ITEM_PUMP1   = 0x04;
static const uint8_t ITEM_PUMP2   = 0x05;
static const uint8_t ITEM_LIGHT   = 0x11;

void SpaProtocol::setArmed(bool a) {
    armed_ = a;
    if (!a) { reg_ = Unregistered; channel_ = 0; owe_ = OweNone; }
}

void SpaProtocol::enqueue(uint8_t type, uint8_t arg) {
    if (qCount_ >= QCAP) return;
    queue_[(qHead_ + qCount_) % QCAP] = {type, arg};
    qCount_++;
}
void SpaProtocol::cmdSetTemp(uint8_t t)   { enqueue(CMD_SET_TEMP, t); }
void SpaProtocol::cmdTogglePump1()        { enqueue(CMD_TOGGLE, ITEM_PUMP1); }
void SpaProtocol::cmdTogglePump2()        { enqueue(CMD_TOGGLE, ITEM_PUMP2); }
void SpaProtocol::cmdToggleLight()        { enqueue(CMD_TOGGLE, ITEM_LIGHT); }

size_t SpaProtocol::encodeCommand(const Cmd& c, uint8_t* out, size_t cap) {
    if (c.type == CMD_SET_TEMP) {
        uint8_t body[] = {channel_, 0xBF, 0x20, c.arg};
        return balboa_build_frame(out, cap, body, sizeof body);
    }
    uint8_t body[] = {channel_, 0xBF, 0x11, c.arg, 0x00};
    return balboa_build_frame(out, cap, body, sizeof body);
}

void SpaProtocol::onFrame(const uint8_t* f, size_t len, uint32_t nowMs) {
    if (len < 5) return;
    uint8_t ch = f[2], t0 = f[3], t1 = f[4];
    const uint8_t* payload = f + 5;
    size_t plen = (len >= 7) ? len - 7 : 0;
    if (ch == 0xFF && t0 == 0xAF && t1 == 0x13) {   // status broadcast
        parseStatus(payload, plen, state_, nowMs);
        return;
    }
    // Registration handshake added in Task 5.
    (void)payload; (void)plen;
}

size_t SpaProtocol::pollTx(uint8_t* out, size_t cap) {
    (void)out; (void)cap;
    return 0;   // Task 5 implements the owed-frame responses
}
