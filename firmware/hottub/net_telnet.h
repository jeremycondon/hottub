#pragma once
#include <WiFi.h>

// Minimal, robust telnet server (default port 23) for the debug shell and the
// log mirror. Replaces the TelnetStream library.
//
// TelnetStream's read()/available() both re-accept the listening socket
// whenever the client's RX buffer is momentarily empty. On ESP32 core 3.x
// server.available()==accept() returns an EMPTY client when no *new* connection
// is pending, which clobbers the live connection — so any command whose bytes
// span more than one loop() iteration gets truncated (status -> stat). The
// heavier D3 loop (HomeSpan + bus + metrics polling) widened that window and
// made it fire constantly.
//
// We instead hold ONE persistent client and only accept() when hasClient()
// reports a genuinely new connection, so the socket's buffered bytes are never
// thrown away between reads.
class TelnetServer : public Print {
public:
    explicit TelnetServer(uint16_t port) : server_(port) {}

    void begin() {
        server_.begin();
        server_.setNoDelay(true);
    }

    // Call once per loop(): pick up a new client, reap a dead one. A second
    // concurrent connection is refused so it can't steal the active session.
    void poll() {
        if (server_.hasClient()) {
            WiFiClient incoming = server_.accept();
            if (client_ && client_.connected()) incoming.stop();   // keep the first
            else                                client_ = incoming;
        }
        if (client_ && !client_.connected()) client_.stop();       // reap on disconnect
    }

    int available() { return client_.connected() ? client_.available() : 0; }
    int read()      { return client_.connected() ? client_.read()      : -1; }

    size_t write(uint8_t b) override {
        if (client_.connected()) client_.write(b);
        return 1;                                                  // never block the log
    }
    size_t write(const uint8_t* buf, size_t n) override {
        if (client_.connected()) client_.write(buf, n);
        return n;
    }
    using Print::write;

private:
    WiFiServer server_;
    WiFiClient client_;
};
