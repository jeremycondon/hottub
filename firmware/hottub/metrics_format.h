#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include "spa_state.h"

// Renders spa + device metrics in Prometheus text exposition format.
// Pure function (no Arduino/WiFi dependency) so it can be exercised from
// host-side tests, same as the rest of the protocol layer.

inline size_t appendMetric(char* buf, size_t cap, size_t n, const char* fmt, ...) {
    if (n >= cap) return n;
    va_list args;
    va_start(args, fmt);
    int w = vsnprintf(buf + n, cap - n, fmt, args);
    va_end(args);
    if (w <= 0) return n;
    size_t remaining = cap - n;
    size_t written = (size_t)w < remaining ? (size_t)w : remaining;
    return n + written;
}

// armed: RS485 transmit armed. chipTempKnown/chipTempC: ESP32-S3 die temp.
// uptimeMs: millis() since boot. rssiDbm: WiFi.RSSI().
inline size_t renderMetrics(const SpaState& s, bool armed, bool chipTempKnown, float chipTempC,
                             uint32_t uptimeMs, int rssiDbm, char* buf, size_t bufCap) {
    size_t n = 0;
    n = appendMetric(buf, bufCap, n, "# HELP hottub_current_temp_fahrenheit Current water temperature.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_current_temp_fahrenheit gauge\n");
    if (s.tempKnown) {
        n = appendMetric(buf, bufCap, n, "hottub_current_temp_fahrenheit %u\n", s.currentTempF);
    }

    n = appendMetric(buf, bufCap, n, "# HELP hottub_set_temp_fahrenheit Target water temperature.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_set_temp_fahrenheit gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_set_temp_fahrenheit %u\n", s.setTempF);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_pump1_speed Pump 1 speed (0=off, 1=low, 2=high).\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_pump1_speed gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_pump1_speed %u\n", s.pump1);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_pump2 Pump 2 state.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_pump2 gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_pump2 %d\n", s.pump2 ? 1 : 0);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_light Light state.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_light gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_light %d\n", s.light ? 1 : 0);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_circulation_pump Circulation pump state.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_circulation_pump gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_circulation_pump %d\n", s.circ ? 1 : 0);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_heating Heater active.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_heating gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_heating %d\n", s.heating ? 1 : 0);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_high_range Thermostat in high-range mode.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_high_range gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_high_range %d\n", s.highRange ? 1 : 0);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_spa_data_stale 1 if no RS485 status frame has been seen recently.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_spa_data_stale gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_spa_data_stale %d\n", s.stale ? 1 : 0);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_armed 1 if RS485 transmit is armed.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_armed gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_armed %d\n", armed ? 1 : 0);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_chip_temp_celsius ESP32-S3 die temperature.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_chip_temp_celsius gauge\n");
    if (chipTempKnown) {
        n = appendMetric(buf, bufCap, n, "hottub_chip_temp_celsius %.1f\n", (double)chipTempC);
    }

    n = appendMetric(buf, bufCap, n, "# HELP hottub_wifi_rssi_dbm WiFi signal strength.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_wifi_rssi_dbm gauge\n");
    n = appendMetric(buf, bufCap, n, "hottub_wifi_rssi_dbm %d\n", rssiDbm);

    n = appendMetric(buf, bufCap, n, "# HELP hottub_uptime_seconds Seconds since boot.\n");
    n = appendMetric(buf, bufCap, n, "# TYPE hottub_uptime_seconds counter\n");
    n = appendMetric(buf, bufCap, n, "hottub_uptime_seconds %lu\n", (unsigned long)(uptimeMs / 1000));

    return n;
}
