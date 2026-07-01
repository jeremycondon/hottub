/*
 * D1 Sniffer — Balboa BP501 RS485 bus monitor
 *
 * Waveshare ESP32-S3-RS485-CAN pin mapping:
 *   RS485 TX        GPIO17  (UART1)
 *   RS485 RX        GPIO18  (UART1)
 *   RS485 TX-Enable GPIO21
 *
 * Connect:
 *   Balboa A+ → board A+ terminal
 *   Balboa B- → board B- terminal
 *   Balboa GND → board GND  (recommended)
 *
 * First boot: connect to "HotTub-Sniffer" AP and enter WiFi credentials.
 * Subsequent boots connect automatically.
 *
 * OTA:     make flash-ota         (or: make flash-ota OTA_HOST=<ip>)
 * Logs:    telnet hottub-sniffer.local
 *          (USB Serial also works as usual)
 *
 * This sketch is READ-ONLY on the RS485 bus — it never transmits.
 */

#include <HardwareSerial.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <TelnetStream.h>

#include "secrets.h"        // OTA_PASSWORD — gitignored; copy from secrets.h.example
#include "balboa_frame.h"   // Balboa RS485 framing + CRC (host-tested in test/)

// ── Config ────────────────────────────────────────────────────────────────────

static constexpr const char* OTA_HOSTNAME = "hottub-sniffer";

// Waveshare ESP32-S3-RS485-CAN RS485 pins
static constexpr int RS485_TX   = 17;
static constexpr int RS485_RX   = 18;
static constexpr int RS485_DE   = 21;   // Driver Enable: HIGH=transmit, LOW=receive
static constexpr int RS485_BAUD = 115200;

// ── Log helper ────────────────────────────────────────────────────────────────
// Forwards all output to both USB Serial and the Telnet session (if connected).

struct DualPrint : public Print {
    size_t write(uint8_t c) override {
        Serial.write(c);
        TelnetStream.write(c);
        return 1;
    }
    size_t write(const uint8_t *buf, size_t size) override {
        Serial.write(buf, size);
        TelnetStream.write(buf, size);
        return size;
    }
} Log;

// CRC and framing live in balboa_frame.h (host-tested against captured frames).
static BalboaFrameReader reader;

// ── Decode helpers ────────────────────────────────────────────────────────────

static const char* pumpStateStr(uint8_t v) {
    switch (v) {
        case 0: return "off";
        case 1: return "low";
        case 2: return "high";
        default: return "?";
    }
}

static void printHex(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] < 0x10) Log.print('0');
        Log.print(buf[i], HEX);
        Log.print(' ');
    }
}

// ── Status message decoder (type 0xFF 0xAF) ───────────────────────────────────
//
// Payload layout (bytes after the 2-byte type, 0-indexed):
//   0       : hour
//   1       : minute
//   2       : flags (heating mode, temp scale, etc.)
//   3       : current temp (°F or °C×2; 0xFF = sensor not ready)
//   4       : flags2
//   5       : set temp
//   6       : pump / jet status  bits[1:0]=pump1, bits[3:2]=pump2
//   7       : flags3 (circ, blower, light)
//   8..end  : additional flags
//
// Note: exact offsets vary slightly by firmware revision. Validate against
// your observed frames. See balboa-protocol.md for full reference.

static void decodeStatusMessage(const uint8_t *payload, size_t payloadLen) {
    if (payloadLen < 9) {
        Log.println("  [status] payload too short");
        return;
    }

    uint8_t hour       = payload[0];
    uint8_t minute     = payload[1];
    uint8_t flags      = payload[2];
    uint8_t curTempRaw = payload[3];
    uint8_t setTempRaw = payload[5];
    uint8_t pumpFlags  = payload[6];
    uint8_t miscFlags  = payload[7];

    bool tempIsCelsius = (flags >> 0) & 0x01;
    bool isHeating     = (flags >> 4) & 0x01;
    bool isHighRange   = (flags >> 2) & 0x01;

    uint8_t pump1 = (pumpFlags >> 0) & 0x03;
    uint8_t pump2 = (pumpFlags >> 2) & 0x03;
    bool    light = (miscFlags >> 0) & 0x01;
    bool    circ  = (miscFlags >> 1) & 0x01;

    Log.printf("  [status] %02d:%02d", hour, minute);
    if (curTempRaw == 0xFF) {
        Log.print("  temp=INIT");
    } else {
        float curTemp = tempIsCelsius ? curTempRaw / 2.0f : curTempRaw;
        float setTemp = tempIsCelsius ? setTempRaw / 2.0f : setTempRaw;
        Log.printf("  cur=%.1f%s  set=%.1f%s",
            curTemp, tempIsCelsius ? "C" : "F",
            setTemp, tempIsCelsius ? "C" : "F");
    }
    Log.printf("  heating=%s  range=%s",
        isHeating ? "YES" : "no",
        isHighRange ? "high" : "low");
    Log.printf("  pump1=%s  pump2=%s  light=%s  circ=%s",
        pumpStateStr(pump1), pumpStateStr(pump2),
        light ? "on" : "off",
        circ  ? "on" : "off");
    Log.println();
}

// ── Frame processor ───────────────────────────────────────────────────────────

// Frame layout: 7E LEN CH TYPEHI TYPELO <payload...> CRC 7E
// Called only for CRC-valid frames (the reader rejects bad ones).
static void processFrame(const uint8_t *frame, size_t len) {
    uint8_t channel = frame[2];
    uint8_t typeHi  = frame[3];
    uint8_t typeLo  = frame[4];

    // Raw hex (CRC already verified by the reader)
    Log.print("FRAME [");
    Log.print(len);
    Log.print("] ");
    printHex(frame, len);
    Log.println(" CRC:OK");

    // Payload sits between the type bytes and the CRC.
    const uint8_t *payload    = frame + 5;
    size_t         payloadLen = (len >= 7) ? len - 7 : 0;   // 7E,LEN,CH,TYPE(2),CRC,7E

    if (typeHi == 0xAF && typeLo == 0x13) {
        decodeStatusMessage(payload, payloadLen);
    } else {
        Log.printf("  [chan 0x%02X  type 0x%02X 0x%02X] payload(%zu): ",
                   channel, typeHi, typeLo, payloadLen);
        printHex(payload, payloadLen);
        Log.println();
    }
}

// ── Arduino setup / loop ──────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Balboa BP501 RS485 Sniffer (D1) ===");
    Serial.println("Connecting to WiFi...");

    WiFi.setHostname(OTA_HOSTNAME);
    WiFiManager wm;
    wm.autoConnect("HotTub-Sniffer");   // blocks until connected (captive portal on first boot)

    TelnetStream.begin();

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]()  { Log.println("[OTA] Starting..."); });
    ArduinoOTA.onEnd([]()    { Log.println("[OTA] Done — rebooting."); });
    ArduinoOTA.onError([](ota_error_t e) { Log.printf("[OTA] Error %u\n", e); });
    ArduinoOTA.begin();

    Log.println("\n=== Balboa BP501 RS485 Sniffer (D1) ===");
    Log.println("Board:  Waveshare ESP32-S3-RS485-CAN");
    Log.printf("WiFi:   %s  (%s)\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    Log.printf("OTA:    %s.local  (password set)\n", OTA_HOSTNAME);
    Log.printf("RS485:  TX=GPIO%d  RX=GPIO%d  DE=GPIO%d  %d baud\n",
        RS485_TX, RS485_RX, RS485_DE, RS485_BAUD);
    Log.println("Mode:   RECEIVE-ONLY (never transmits)");
    Log.println("========================================\n");

    // Hold DE low — receive-only for the entire session
    pinMode(RS485_DE, OUTPUT);
    digitalWrite(RS485_DE, LOW);

    Serial1.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);

    Log.println("Listening...\n");
}

void loop() {
    ArduinoOTA.handle();

    while (Serial1.available()) {
        if (reader.push((uint8_t)Serial1.read())) {
            processFrame(reader.buf, reader.len);
        }
    }
}
