/*
 * HotTub — Balboa BP501 controller (D2: bidirectional Balboa comms).
 * Boots READ-ONLY. Transmit is gated behind the telnet `arm` command (Task 7).
 *
 * OTA:   make ota          Logs: telnet hottub.local
 */
#include <HardwareSerial.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <TelnetStream.h>

#include "secrets.h"
#include "balboa_frame.h"
#include "balboa_bus.h"

static constexpr const char* OTA_HOSTNAME = "hottub";
static constexpr int RS485_TX = 17, RS485_RX = 18, RS485_DE = 21;
static constexpr int RS485_BAUD = 115200;

struct DualPrint : public Print {
    size_t write(uint8_t c) override { Serial.write(c); TelnetStream.write(c); return 1; }
    size_t write(const uint8_t *b, size_t n) override { Serial.write(b, n); TelnetStream.write(b, n); return n; }
} Log;

static BalboaFrameReader reader;
static BalboaBus bus;

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
    pinMode(RS485_DE, OUTPUT);
    digitalWrite(RS485_DE, LOW);            // receive-only for now
    Serial1.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    Log.printf("\n=== HotTub controller ===\nWiFi %s (%s)\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void loop() {
    ArduinoOTA.handle();
    while (Serial1.available()) {
        if (reader.push((uint8_t)Serial1.read())) {
            Log.printf("FRAME[%u] ch=0x%02X type=0x%02X%02X\n",
                (unsigned)reader.len, reader.buf[2], reader.buf[3], reader.buf[4]);
        }
    }
}
