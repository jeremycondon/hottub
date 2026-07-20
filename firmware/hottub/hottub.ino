/*
 * HotTub — Balboa BP501 controller (D2).
 * Boots ARMED so HomeKit can transmit autonomously. Telnet `disarm`/`arm`
 * remain available to stop/resume TX for manual debugging.
 *
 * Telnet (hottub.local): status | version | uptime | arm | disarm | temp <F> | pump1 | pump2 | light | raw | unpair
 * OTA: make ota
 */
#include <HardwareSerial.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include "HomeSpan.h"

#include "net_telnet.h"

#include "secrets.h"
#include "balboa_bus.h"
#include "spa_reconcile.h"
#include "homekit_spa.h"

// Optional Prometheus /metrics endpoint for Grafana dashboards (see
// docs/monitoring.md). Set to 0 to build without it.
#ifndef ENABLE_METRICS
#define ENABLE_METRICS 1
#endif
#if ENABLE_METRICS
#include "metrics.h"
#endif

static constexpr const char* OTA_HOSTNAME = "hottub";
// Compile-time build stamp: bumps on every rebuild, so after an OTA you can
// telnet `version` and confirm the new image is actually running.
static constexpr const char* FW_BUILD = __DATE__ " " __TIME__;
static constexpr int RS485_TX = 17, RS485_RX = 18, RS485_DE = 21;
static constexpr int RS485_BAUD = 115200;

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

static TelnetServer telnet(23);

struct DualPrint : public Print {
    size_t write(uint8_t c) override { Serial.write(c); telnet.write(c); return 1; }
    size_t write(const uint8_t *b, size_t n) override { Serial.write(b, n); telnet.write(b, n); return n; }
} Log;

static BalboaBus bus;
#if ENABLE_METRICS
static MetricsServer metrics;
#endif
static bool          rawDump = false;
static SpaState      lastPrinted;
static char          line[32];
static size_t        lineLen = 0;
static SpaReconciler reconciler;
static SpaHomeKit    homekit(reconciler);
static uint32_t      lastStatusMs = 0;

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

// Rollover-safe uptime: esp_timer is 64-bit microseconds since boot.
static void formatUptime(char* out, size_t cap) {
    uint32_t s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t d = s / 86400; s %= 86400;
    uint32_t h = s / 3600;  s %= 3600;
    uint32_t m = s / 60;    s %= 60;
    snprintf(out, cap, "%lud %02luh %02lum %02lus",
             (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
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
    else if (!strcmp(cmd, "version")) { Log.printf("[fw] D3 build %s\n", FW_BUILD); }
    else if (!strcmp(cmd, "uptime"))  { char u[32]; formatUptime(u, sizeof u); Log.printf("[uptime] %s\n", u); }
    else if (!strcmp(cmd, "arm"))    { bus.proto.setArmed(true);  Log.println("[armed] TX enabled"); }
    else if (!strcmp(cmd, "disarm")) { bus.proto.setArmed(false); Log.println("[disarmed] read-only"); }
    else if (!strcmp(cmd, "raw"))    { rawDump = !rawDump; Log.printf("[raw] %s\n", rawDump?"on":"off"); }
    else if (!strncmp(cmd, "temp ", 5)) {
        if (!bus.proto.armed()) { Log.println("[err] arm first"); return; }
        int t = atoi(cmd + 5);
        if (t < 40 || t > 104) { Log.println("[err] temp out of range 40-104F"); return; }
        bus.proto.cmdSetTemp((uint8_t)t);
        Log.println("[queued] set temp");
    }
    else if (!strcmp(cmd, "pump1")) { if (bus.proto.armed()){ bus.proto.cmdTogglePump1(); Log.println("[queued] pump1"); } else Log.println("[err] arm first"); }
    else if (!strcmp(cmd, "pump2")) { if (bus.proto.armed()){ bus.proto.cmdTogglePump2(); Log.println("[queued] pump2"); } else Log.println("[err] arm first"); }
    else if (!strcmp(cmd, "light")) { if (bus.proto.armed()){ bus.proto.cmdToggleLight(); Log.println("[queued] light"); } else Log.println("[err] arm first"); }
    else if (!strcmp(cmd, "unpair")) {
        // Delete HomeKit pairing data so the device can be re-added in the Home
        // app (setup code 466-37-726). 'H' keeps WiFi creds — unlike 'F' — then
        // reboots, so this stays reachable without opening the enclosure.
        Log.println("[homekit] deleting pairing data, rebooting (setup code 466-37-726)...");
        Serial.flush();
        homeSpan.processSerialCommand("H");
    }
    else if (cmd[0]) { Log.printf("[?] unknown: %s\n", cmd); }
}

static void pollTelnet() {
    while (telnet.available()) {
        char c = telnet.read();
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
    telnet.begin();
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
    bus.begin(RS485_RX, RS485_TX, RS485_DE, RS485_BAUD);
    bus.onRawFrame = rawFrameDump;

#if ENABLE_METRICS
    metrics.begin();
    Log.printf("[metrics] http://%s:%u/metrics\n", WiFi.localIP().toString().c_str(), metrics.port());
#endif

    bus.proto.setArmed(true);                       // always-armed for autonomous HomeKit

    // HomeSpan owns the ESP32's single mDNS responder, and its begin() runs
    // after ArduinoOTA's, so its hostname wins. Pin it to "hottub" (no -MAC
    // suffix) so the device stays reachable as hottub.local for OTA + telnet
    // instead of only HomeSpan's default HomeSpan-<MAC>.local.
    homeSpan.setHostNameSuffix("");
    // Pairing setup code is HomeSpan's default 466-37-726 (not pinned via
    // setPairingCode() on purpose: that regenerates SRP data + writes NVS every
    // boot). Telnet `unpair` clears pairing for a re-add. See docs/monitoring.md.
    homeSpan.begin(Category::Thermostats, "HotTub", "hottub");
    homekit.build();

    Log.printf("\n=== HotTub controller (D3) build %s ===\nWiFi %s (%s)\nArmed for HomeKit control; telnet `disarm` to stop TX.\n",
        FW_BUILD, WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void loop() {
    pollWifiWatchdog(millis());
    ArduinoOTA.handle();
    telnet.poll();
    pollTelnet();
    bus.poll(millis());
    pollChipTemp(millis());
#if ENABLE_METRICS
    metrics.update(bus.proto.state(), bus.proto.armed(), chipTempKnown, chipTempC, millis());
    metrics.handle();
#endif
    homeSpan.poll();

    const SpaState& s = bus.proto.state();

    // Print state on change.
    if (stateChanged(s, lastPrinted)) { printState(); lastPrinted = s; }

    // A fresh status frame arrived: advance the reconciler and push actual
    // state back to HomeKit characteristics.
    if (s.lastUpdateMs != lastStatusMs) {
        lastStatusMs = s.lastUpdateMs;
        reconciler.tick(s, bus.proto, millis());
        homekit.refresh(s);
    }
}
