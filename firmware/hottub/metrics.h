#pragma once
#include <WebServer.h>
#include "spa_state.h"

// Optional Prometheus exporter. Serves spa + device metrics at /metrics for
// scraping by a Prometheus server, which Grafana then reads (see
// docs/monitoring.md). Gated behind ENABLE_METRICS in hottub.ino.
class MetricsServer {
public:
    void begin();
    void handle();
    void update(const SpaState& state, bool armed, bool chipTempKnown, float chipTempC, uint32_t uptimeMs);

private:
    WebServer server_;
    SpaState  state_;
    bool      armed_         = false;
    bool      chipTempKnown_ = false;
    float     chipTempC_     = 0;
    uint32_t  uptimeMs_      = 0;

    void handleMetrics();
};
