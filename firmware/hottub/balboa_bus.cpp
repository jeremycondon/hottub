#include "balboa_bus.h"

void BalboaBus::begin(int rx, int tx, int de, uint32_t baud) {
    dePin_ = de;
    pinMode(dePin_, OUTPUT);
    digitalWrite(dePin_, LOW);                      // receive
    Serial1.begin(baud, SERIAL_8N1, rx, tx);
}

void BalboaBus::transmit(const uint8_t* frame, size_t n) {
    digitalWrite(dePin_, HIGH);                     // drive the bus
    Serial1.write(frame, n);
    Serial1.flush();                                // block until fully shifted out
    digitalWrite(dePin_, LOW);                      // release to receive
}

void BalboaBus::poll(uint32_t nowMs) {
    while (Serial1.available()) {
        if (reader_.push((uint8_t)Serial1.read())) {
            if (onRawFrame) onRawFrame(reader_.buf, reader_.len);
            proto.onFrame(reader_.buf, reader_.len, nowMs);
            uint8_t out[16];
            size_t n = proto.pollTx(out, sizeof out);
            if (n) transmit(out, n);
        }
    }
}
