/*
 * D1 Sniffer — Balboa BP501 RS485 bus monitor
 *
 * Waveshare ESP32-S3-RS485-CAN pin mapping:
 *   RS485 TX       GPIO17  (UART1)
 *   RS485 RX       GPIO18  (UART1)
 *   RS485 TX-Enable GPIO21
 *
 * Connect:
 *   Balboa A+ → board A+ terminal
 *   Balboa B- → board B- terminal
 *   Balboa GND → board GND  (recommended)
 *
 * Open Serial Monitor at 115200 baud.
 * This sketch is READ-ONLY — it never transmits on the RS485 bus.
 */

#include <HardwareSerial.h>

// Waveshare ESP32-S3-RS485-CAN RS485 pins
static constexpr int RS485_TX   = 17;
static constexpr int RS485_RX   = 18;
static constexpr int RS485_DE   = 21;   // Driver Enable: HIGH=transmit, LOW=receive

// We're sniffer-only — keep DE low the whole time
static constexpr int RS485_BAUD = 115200;

// Balboa frame constants
static constexpr uint8_t MSG_START = 0xFF;
static constexpr uint8_t MSG_END   = 0x7E;

// Maximum realistic Balboa frame length
static constexpr size_t MAX_FRAME = 64;

// ── CRC ──────────────────────────────────────────────────────────────────────

static uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x02;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
        }
    }
    return crc;
}

// ── Frame buffer ──────────────────────────────────────────────────────────────

static uint8_t  frameBuf[MAX_FRAME];
static size_t   frameLen = 0;
static bool     inFrame  = false;

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
        if (buf[i] < 0x10) Serial.print('0');
        Serial.print(buf[i], HEX);
        Serial.print(' ');
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
        Serial.println("  [status] payload too short");
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

    Serial.printf("  [status] %02d:%02d", hour, minute);
    if (curTempRaw == 0xFF) {
        Serial.print("  temp=INIT");
    } else {
        float curTemp = tempIsCelsius ? curTempRaw / 2.0f : curTempRaw;
        float setTemp = tempIsCelsius ? setTempRaw / 2.0f : setTempRaw;
        Serial.printf("  cur=%.1f%s  set=%.1f%s",
            curTemp, tempIsCelsius ? "C" : "F",
            setTemp, tempIsCelsius ? "C" : "F");
    }
    Serial.printf("  heating=%s  range=%s",
        isHeating ? "YES" : "no",
        isHighRange ? "high" : "low");
    Serial.printf("  pump1=%s  pump2=%s  light=%s  circ=%s",
        pumpStateStr(pump1), pumpStateStr(pump2),
        light ? "on" : "off",
        circ  ? "on" : "off");
    Serial.println();
}

// ── Frame processor ───────────────────────────────────────────────────────────

static void processFrame(const uint8_t *frame, size_t len) {
    // Minimum valid frame: start(1) + len(1) + type(2) + crc(1) + end(1) = 6
    if (len < 6) return;

    // frame[0]=0xFF  frame[1]=msgLen  frame[len-2]=crc  frame[len-1]=0x7E
    uint8_t msgLen   = frame[1];
    uint8_t rxCrc    = frame[len - 2];
    uint8_t calcCrc  = crc8(frame + 1, len - 3);   // over len byte through last payload byte

    bool crcOk = (rxCrc == calcCrc);

    uint8_t typeHi = frame[2];
    uint8_t typeLo = frame[3];

    // Raw hex
    Serial.print("FRAME [");
    Serial.print(len);
    Serial.print("] ");
    printHex(frame, len);
    Serial.printf(" CRC:%s\n", crcOk ? "OK" : "FAIL");

    if (!crcOk) return;

    // Payload starts after start(1) + len(1) + type(2)
    const uint8_t *payload = frame + 4;
    size_t payloadLen = (len >= 6) ? len - 6 : 0;   // exclude start,len,type(2),crc,end

    if (typeHi == 0xFF && typeLo == 0xAF) {
        decodeStatusMessage(payload, payloadLen);
    } else {
        Serial.printf("  [type 0x%02X 0x%02X] payload(%zu): ", typeHi, typeLo, payloadLen);
        printHex(payload, payloadLen);
        Serial.println();
    }
}

// ── Arduino setup / loop ──────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Balboa BP501 RS485 Sniffer (D1) ===");
    Serial.println("Board:  Waveshare ESP32-S3-RS485-CAN");
    Serial.printf("RS485:  TX=GPIO%d  RX=GPIO%d  DE=GPIO%d  %d baud\n",
        RS485_TX, RS485_RX, RS485_DE, RS485_BAUD);
    Serial.println("Mode:   RECEIVE-ONLY (never transmits)");
    Serial.println("========================================\n");

    // Hold DE low — receive-only for the entire session
    pinMode(RS485_DE, OUTPUT);
    digitalWrite(RS485_DE, LOW);

    // Start UART1 on the RS485 pins
    Serial1.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);

    Serial.println("Listening...\n");
}

void loop() {
    while (Serial1.available()) {
        uint8_t b = Serial1.read();

        if (b == MSG_START && !inFrame) {
            // Start of a new frame
            frameLen = 0;
            inFrame  = true;
        }

        if (inFrame) {
            if (frameLen < MAX_FRAME) {
                frameBuf[frameLen++] = b;
            } else {
                // Overflow — discard and reset
                Serial.println("[sniffer] frame overflow, resetting");
                inFrame  = false;
                frameLen = 0;
            }

            if (b == MSG_END && frameLen > 2) {
                // End of frame
                processFrame(frameBuf, frameLen);
                inFrame  = false;
                frameLen = 0;
            }
        }
    }
}
