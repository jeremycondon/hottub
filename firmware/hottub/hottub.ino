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
#include "HomeSpan.h"

#include "secrets.h"
#include "balboa_bus.h"

static constexpr const char* OTA_HOSTNAME = "hottub";
static constexpr int RS485_TX = 17, RS485_RX = 18, RS485_DE = 21;
static constexpr int RS485_BAUD = 115200;
static constexpr uint32_t ARM_TIMEOUT_MS = 5UL * 60UL * 1000UL;

struct DualPrint : public Print {
    size_t write(uint8_t c) override { Serial.write(c); TelnetStream.write(c); return 1; }
    size_t write(const uint8_t *b, size_t n) override { Serial.write(b, n); TelnetStream.write(b, n); return n; }
} Log;

static BalboaBus bus;
static bool     rawDump = false;
static uint32_t armedAt = 0;
static SpaState lastPrinted;
static char     line[32];
static size_t   lineLen = 0;

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
    Log.printf("[spa] cur=%s set=%uF pump1=%u pump2=%s light=%s circ=%s heat=%s range=%s  reg=%d ch=0x%02X armed=%d\n",
        cur,
        s.setTempF, s.pump1, s.pump2?"on":"off", s.light?"on":"off",
        s.circ?"on":"off", s.heating?"on":"off", s.highRange?"high":"low",
        (int)bus.proto.regState(), bus.proto.channel(), bus.proto.armed());
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
    bus.proto.setArmed(true);                       // always-armed for autonomous HomeKit

    homeSpan.begin(Category::Thermostats, "HotTub");
    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name("HotTub");

    Log.printf("\n=== HotTub controller (D2) ===\nWiFi %s (%s)\nRead-only until `arm`.\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void loop() {
    ArduinoOTA.handle();
    pollTelnet();
    bus.poll(millis());
    homeSpan.poll();

    // Auto-disarm after inactivity.
    if (bus.proto.armed() && millis() - armedAt > ARM_TIMEOUT_MS) {
        bus.proto.setArmed(false);
        Log.println("[disarmed] idle timeout");
    }

    // Print state on change.
    const SpaState& s = bus.proto.state();
    if (stateChanged(s, lastPrinted)) { printState(); lastPrinted = s; }
}
