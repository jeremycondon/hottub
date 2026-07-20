#pragma once
#include <WebServer.h>
#include "spa_state.h"

// Optional Prometheus exporter. Serves spa + device metrics at /metrics for
// scraping by a Prometheus server, which Grafana then reads (see
// docs/monitoring.md). Gated behind ENABLE_METRICS in hottub.ino.
//
// Defaults to port 9100 (the Prometheus exporter convention), NOT 80: HomeSpan's
// HAP server owns 80, and a second WebServer there breaks HomeKit pairing.
class MetricsServer {
public:
    explicit MetricsServer(uint16_t port = 9100) : server_(port), port_(port) {}
    uint16_t port() const { return port_; }
    void begin();
    void handle();
    void update(const SpaState& state, bool armed, bool chipTempKnown, float chipTempC, uint32_t uptimeMs);

private:
    WebServer server_;
    uint16_t  port_;
    SpaState  state_;
    bool      armed_         = false;
    bool      chipTempKnown_ = false;
    float     chipTempC_     = 0;
    uint32_t  uptimeMs_      = 0;

    void handleMetrics();
};
