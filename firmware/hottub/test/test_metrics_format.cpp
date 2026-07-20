#include "../metrics_format.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static bool contains(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != nullptr;
}

int main() {
    SpaState s;
    s.tempKnown  = true;
    s.currentTempF = 101;
    s.setTempF   = 102;
    s.pump1      = 2;
    s.pump2      = true;
    s.light      = false;
    s.circ       = true;
    s.heating    = true;
    s.highRange  = true;
    s.stale      = false;

    char buf[2048];
    size_t n = renderMetrics(s, /*armed=*/true, /*chipTempKnown=*/true, 42.5f,
                              /*uptimeMs=*/125000, /*rssiDbm=*/-58, buf, sizeof buf);
    assert(n > 0 && n < sizeof buf);
    assert(strlen(buf) == n);

    assert(contains(buf, "hottub_current_temp_fahrenheit 101\n"));
    assert(contains(buf, "hottub_set_temp_fahrenheit 102\n"));
    assert(contains(buf, "hottub_pump1_speed 2\n"));
    assert(contains(buf, "hottub_pump2 1\n"));
    assert(contains(buf, "hottub_light 0\n"));
    assert(contains(buf, "hottub_circulation_pump 1\n"));
    assert(contains(buf, "hottub_heating 1\n"));
    assert(contains(buf, "hottub_high_range 1\n"));
    assert(contains(buf, "hottub_spa_data_stale 0\n"));
    assert(contains(buf, "hottub_armed 1\n"));
    assert(contains(buf, "hottub_chip_temp_celsius 42.5\n"));
    assert(contains(buf, "hottub_wifi_rssi_dbm -58\n"));
    assert(contains(buf, "hottub_uptime_seconds 125\n"));

    // Unknown current temp / chip temp: HELP+TYPE present, sample omitted.
    SpaState s2;
    s2.tempKnown = false;
    char buf2[2048];
    n = renderMetrics(s2, false, /*chipTempKnown=*/false, 0, 0, -70, buf2, sizeof buf2);
    assert(contains(buf2, "# TYPE hottub_current_temp_fahrenheit gauge\n"));
    assert(!contains(buf2, "hottub_current_temp_fahrenheit 0\n"));
    assert(!contains(buf2, "hottub_chip_temp_celsius 0.0\n"));

    // Tiny buffer must not overflow or corrupt (appendMetric clamps at cap).
    char tiny[8];
    size_t tn = renderMetrics(s, true, true, 42.5f, 1000, -50, tiny, sizeof tiny);
    assert(tn <= sizeof tiny);

    printf("ALL TESTS PASSED\n");
    return 0;
}
