#include "metrics.h"
#include "metrics_format.h"
#include <WiFi.h>

void MetricsServer::begin() {
    server_.on("/metrics", HTTP_GET, [this]() { handleMetrics(); });
    server_.begin();
}

void MetricsServer::handle() {
    server_.handleClient();
}

void MetricsServer::update(const SpaState& state, bool armed, bool chipTempKnown, float chipTempC, uint32_t uptimeMs) {
    state_         = state;
    armed_         = armed;
    chipTempKnown_ = chipTempKnown;
    chipTempC_     = chipTempC;
    uptimeMs_      = uptimeMs;
}

void MetricsServer::handleMetrics() {
    char body[2048];
    size_t n = renderMetrics(state_, armed_, chipTempKnown_, chipTempC_, uptimeMs_, WiFi.RSSI(), body, sizeof body);
    server_.send(200, "text/plain; version=0.0.4; charset=utf-8", body, n);
}
