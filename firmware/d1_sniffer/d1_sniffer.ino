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

// Set false once the status-message byte offsets are pinned down (D1 done).
static constexpr bool CALIBRATE = true;

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

static const char* pumpStateStr(uint8_t v) {
    switch (v) {
        case 0: return "off";
        case 1: return "low";
        case 2: return "high";
        default: return "?";
    }
}

// Status-message payload offsets for this BP501, pinned empirically from live
// captures (see docs/balboa-protocol.md for the raw frames).
//
// VERIFIED by watching the byte move with the physical state:
//   p2  : current water temp, raw °F  (0xFF = no reading yet / no flow past
//                                       the sensor — treat as "unknown")
//   p10 : flag byte — bit 0x20 = heater active, bit 0x08 = light 1 (see note),
//                     bit 0x04 = temp range (1 = high range)
//   p11 : pumps — bits[1:0] = pump1 (0=off,1=low,2=high),
//                 bits[3:2] = pump2 (0=off, else on; single-speed)
//   p13 : bit 0x02 = circ pump running
//   p20 : set temp, raw °F   (p7 mirrors it — redundant copy)
//
// Cross-checked against Dakoriki/ESPHome-Balboa-Spa (heater/range/circ bits).
// NOTE: light bit is unconfirmed — our capture points to p10 bit 0x08, that
// project uses p14==0x03. Resolve with a clean single-toggle capture.
//
// STILL UNKNOWN (never observed changing meaningfully):
//   p0,p1 : NOT our clock — flip between (0,3) and (3,16) within a second
//           (ref project reads clock at p3/p4; ours looked implausible there)
//   p24 : counter — halved 60→30 during testing
//   temp scale : only ever seen °F; assuming °F until a °C capture appears
static constexpr size_t STATUS_CUR_TEMP = 2;
static constexpr size_t STATUS_FLAGS    = 10;
static constexpr size_t STATUS_PUMPS    = 11;
static constexpr size_t STATUS_CIRC     = 13;
static constexpr size_t STATUS_SET_TEMP = 20;

static void decodeStatusMessage(const uint8_t *payload, size_t payloadLen) {
    if (payloadLen <= STATUS_SET_TEMP) {
        Log.println("  [status] payload too short");
        return;
    }

    // Calibration aid: print every payload byte as index=dec(hex). Set the tub
    // to a known temp/setpoint and look for those decimal values to pin offsets.
    if (CALIBRATE) {
        Log.printf("  [status raw] len=%zu :", payloadLen);
        for (size_t i = 0; i < payloadLen; i++) {
            Log.printf("  %zu=%u(0x%02X)", i, payload[i], payload[i]);
        }
        Log.println();
    }

    uint8_t curTempRaw = payload[STATUS_CUR_TEMP];
    uint8_t setTempRaw = payload[STATUS_SET_TEMP];
    uint8_t flags      = payload[STATUS_FLAGS];
    uint8_t pumps      = payload[STATUS_PUMPS];

    uint8_t pump1     = (pumps >> 0) & 0x03;
    uint8_t pump2     = (pumps >> 2) & 0x03;
    bool    heating   = flags & 0x20;
    bool    light     = flags & 0x08;
    bool    highRange = flags & 0x04;
    bool    circ      = payload[STATUS_CIRC] & 0x02;

    Log.print("  [status]");
    if (curTempRaw == 0xFF) {
        Log.print("  cur=--F");               // no reading right now
    } else {
        Log.printf("  cur=%uF", curTempRaw);   // °F assumed; scale byte TBD
    }
    Log.printf("  set=%uF  pump1=%s  pump2=%s  light=%s  circ=%s  heat=%s  range=%s",
        setTempRaw,
        pumpStateStr(pump1),
        pump2 ? "on" : "off",
        light ? "on" : "off",
        circ  ? "on" : "off",
        heating ? "ON" : "off",
        highRange ? "high" : "low");
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
