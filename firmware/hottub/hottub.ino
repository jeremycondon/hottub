/*
 * HotTub — Balboa BP501 controller (D2).
 * Boots READ-ONLY. Transmit is enabled by the telnet `arm` command and
 * auto-disarms after ARM_TIMEOUT_MS of inactivity.
 *
 * Telnet (hottub.local): status | arm | disarm | temp <F> | pump1 | pump2 | light | raw
 * OTA: make ota
 */
#include <HardwareSerial.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <TelnetStream.h>

#include "secrets.h"
#include "balboa_bus.h"

// Optional Prometheus /metrics endpoint for Grafana dashboards (see
// docs/monitoring.md). Set to 0 to build without it.
#ifndef ENABLE_METRICS
#define ENABLE_METRICS 1
#endif
#if ENABLE_METRICS
#include "metrics.h"
#endif

static constexpr const char* OTA_HOSTNAME = "hottub";
static constexpr int RS485_TX = 17, RS485_RX = 18, RS485_DE = 21;
static constexpr int RS485_BAUD = 115200;
static constexpr uint32_t ARM_TIMEOUT_MS = 5UL * 60UL * 1000UL;

// ESP32-S3 die temperature, not enclosure air temp — it runs hotter than
// ambient (more so under WiFi TX), so these thresholds are set well above
// normal idle readings. Hysteresis avoids warning spam right at the line.
static constexpr float    CHIP_TEMP_WARN_C  = 70.0f;
static constexpr float    CHIP_TEMP_CLEAR_C = 65.0f;
static constexpr uint32_t CHIP_TEMP_POLL_MS = 10UL * 1000UL;

// The ESP32 core auto-reconnects on its own most of the time, but the WiFi
// stack occasionally wedges after a drop and never retries. Since this
// board is sealed in the enclosure, a stuck reconnect needs to self-heal
// rather than wait for someone to power-cycle it.
static constexpr uint32_t WIFI_KICK_MS    = 2UL  * 60UL * 1000UL;
static constexpr uint32_t WIFI_RESTART_MS = 10UL * 60UL * 1000UL;

struct DualPrint : public Print {
    size_t write(uint8_t c) override { Serial.write(c); TelnetStream.write(c); return 1; }
    size_t write(const uint8_t *b, size_t n) override { Serial.write(b, n); TelnetStream.write(b, n); return n; }
} Log;

static BalboaBus bus;
#if ENABLE_METRICS
static MetricsServer metrics;
#endif
static bool     rawDump = false;
static uint32_t armedAt = 0;
static SpaState lastPrinted;
static char     line[32];
static size_t   lineLen = 0;

static float    chipTempC = 0;
static bool     chipTempKnown = false;
static bool     chipTempHot = false;
static uint32_t lastTempPollAt = 0;

static uint32_t wifiDownSince = 0;
static bool     wifiKicked = false;

static void rawFrameDump(const uint8_t* f, size_t n) {
    if (!rawDump) return;
    Log.print("RAW[");
    Log.print((unsigned)n);
    Log.print("]");
    for (size_t i = 0; i < n; i++) {
        Log.print(' ');
        if (f[i] < 0x10) Log.print('0');
        Log.print(f[i], HEX);
    }
    Log.println();
}

static void printState() {
    const SpaState& s = bus.proto.state();
    char cur[8];
    if (s.tempKnown) snprintf(cur, sizeof cur, "%u", s.currentTempF);
    else             snprintf(cur, sizeof cur, "--");
    char chip[8];
    if (chipTempKnown) snprintf(chip, sizeof chip, "%.1fC", chipTempC);
    else               snprintf(chip, sizeof chip, "--");
    Log.printf("[spa] cur=%s set=%uF pump1=%u pump2=%s light=%s circ=%s heat=%s range=%s  reg=%d ch=0x%02X armed=%d chip=%s\n",
        cur,
        s.setTempF, s.pump1, s.pump2?"on":"off", s.light?"on":"off",
        s.circ?"on":"off", s.heating?"on":"off", s.highRange?"high":"low",
        (int)bus.proto.regState(), bus.proto.channel(), bus.proto.armed(), chip);
}

static void pollChipTemp(uint32_t now) {
    if (now - lastTempPollAt < CHIP_TEMP_POLL_MS) return;
    lastTempPollAt = now;
    chipTempC = temperatureRead();
    chipTempKnown = true;
    if (!chipTempHot && chipTempC >= CHIP_TEMP_WARN_C) {
        chipTempHot = true;
        Log.printf("[warn] chip temp %.1fC exceeds %.1fC — check enclosure airflow\n", chipTempC, CHIP_TEMP_WARN_C);
    } else if (chipTempHot && chipTempC <= CHIP_TEMP_CLEAR_C) {
        chipTempHot = false;
        Log.printf("[ok] chip temp back to %.1fC\n", chipTempC);
    }
}

static void pollWifiWatchdog(uint32_t now) {
    if (WiFi.status() == WL_CONNECTED) {
        wifiDownSince = 0;
        wifiKicked = false;
        return;
    }
    if (wifiDownSince == 0) {
        wifiDownSince = now;
        Log.println("[wifi] disconnected");
        return;
    }
    uint32_t downFor = now - wifiDownSince;
    if (!wifiKicked && downFor >= WIFI_KICK_MS) {
        wifiKicked = true;
        Log.println("[wifi] still down after 2min, forcing reconnect");
        WiFi.reconnect();
    } else if (downFor >= WIFI_RESTART_MS) {
        Log.println("[wifi] still down after 10min, restarting");
        Serial.flush();
        delay(100);
        ESP.restart();
    }
}

static bool stateChanged(const SpaState& a, const SpaState& b) {
    return a.currentTempF!=b.currentTempF || a.setTempF!=b.setTempF ||
           a.pump1!=b.pump1 || a.pump2!=b.pump2 || a.light!=b.light ||
           a.circ!=b.circ || a.heating!=b.heating || a.highRange!=b.highRange ||
           a.tempKnown!=b.tempKnown;
}

static void handleCommand(const char* cmd) {
    if (!strcmp(cmd, "status"))      { printState(); }
    else if (!strcmp(cmd, "arm"))    { bus.proto.setArmed(true);  armedAt = millis(); Log.println("[armed] TX enabled"); }
    else if (!strcmp(cmd, "disarm")) { bus.proto.setArmed(false); Log.println("[disarmed] read-only"); }
    else if (!strcmp(cmd, "raw"))    { rawDump = !rawDump; Log.printf("[raw] %s\n", rawDump?"on":"off"); }
    else if (!strncmp(cmd, "temp ", 5)) {
        if (!bus.proto.armed()) { Log.println("[err] arm first"); return; }
        int t = atoi(cmd + 5);
        if (t < 40 || t > 104) { Log.println("[err] temp out of range 40-104F"); return; }
        bus.proto.cmdSetTemp((uint8_t)t);
        armedAt = millis();
        Log.println("[queued] set temp");
    }
    else if (!strcmp(cmd, "pump1")) { if (bus.proto.armed()){ bus.proto.cmdTogglePump1(); armedAt=millis(); Log.println("[queued] pump1"); } else Log.println("[err] arm first"); }
    else if (!strcmp(cmd, "pump2")) { if (bus.proto.armed()){ bus.proto.cmdTogglePump2(); armedAt=millis(); Log.println("[queued] pump2"); } else Log.println("[err] arm first"); }
    else if (!strcmp(cmd, "light")) { if (bus.proto.armed()){ bus.proto.cmdToggleLight(); armedAt=millis(); Log.println("[queued] light"); } else Log.println("[err] arm first"); }
    else if (cmd[0]) { Log.printf("[?] unknown: %s\n", cmd); }
}

static void pollTelnet() {
    while (TelnetStream.available()) {
        char c = TelnetStream.read();
        if (c == '\n' || c == '\r') {
            if (lineLen) { line[lineLen] = 0; handleCommand(line); lineLen = 0; }
        } else if (lineLen < sizeof(line) - 1) {
            line[lineLen++] = c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    WiFi.setHostname(OTA_HOSTNAME);
    WiFiManager wm;
    wm.autoConnect("HotTub-Setup");
    TelnetStream.begin();
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
    bus.begin(RS485_RX, RS485_TX, RS485_DE, RS485_BAUD);
    bus.onRawFrame = rawFrameDump;
#if ENABLE_METRICS
    metrics.begin();
    Log.printf("[metrics] http://%s/metrics\n", WiFi.localIP().toString().c_str());
#endif
    Log.printf("\n=== HotTub controller (D2) ===\nWiFi %s (%s)\nRead-only until `arm`.\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void loop() {
    pollWifiWatchdog(millis());
    ArduinoOTA.handle();
    pollTelnet();
    bus.poll(millis());
    pollChipTemp(millis());
#if ENABLE_METRICS
    metrics.update(bus.proto.state(), bus.proto.armed(), chipTempKnown, chipTempC, millis());
    metrics.handle();
#endif

    // Auto-disarm after inactivity.
    if (bus.proto.armed() && millis() - armedAt > ARM_TIMEOUT_MS) {
        bus.proto.setArmed(false);
        Log.println("[disarmed] idle timeout");
    }

    // Print state on change.
    const SpaState& s = bus.proto.state();
    if (stateChanged(s, lastPrinted)) { printState(); lastPrinted = s; }
}
