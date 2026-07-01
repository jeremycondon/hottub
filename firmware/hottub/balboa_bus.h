#pragma once
#include <Arduino.h>
#include "balboa_frame.h"
#include "spa_control.h"

class BalboaBus {
public:
    void begin(int rxPin, int txPin, int dePin, uint32_t baud);
    void poll(uint32_t nowMs);
    SpaProtocol proto;
private:
    int dePin_ = -1;
    BalboaFrameReader reader_;
    void transmit(const uint8_t* frame, size_t n);
};
