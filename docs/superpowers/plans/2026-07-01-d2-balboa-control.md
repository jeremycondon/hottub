# D2 — Balboa Control Library + Telnet Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bidirectional RS485 comms with the Balboa BP501 — read full spa state and issue every control command, driven and verified over a Telnet command shell.

**Architecture:** Four focused units in `firmware/hottub/`. Pure, host-testable logic (`balboa_frame.h` framing/CRC, `spa_state.h` status decode, `spa_control.{h,cpp}` the `SpaProtocol` brain: registration state machine + command queue + frame encoding) is separated from ESP32 I/O (`balboa_bus.{h,cpp}`: UART1 + DE-pin half-duplex glue). `hottub.ino` wires WiFi/OTA/Telnet (reused from the sniffer) to a command shell. TX is gated behind a runtime `arm` command.

**Tech Stack:** Arduino (ESP32 Arduino Core 3.3.10), arduino-cli sketch profile, WiFiManager, TelnetStream, ArduinoOTA. Host tests: `g++ -std=c++17` + `assert`.

## Global Constraints

- Board/FQBN: `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB` (pin via `sketch.yaml`, build with `--profile hottub`).
- RS485 pins: TX=GPIO17, RX=GPIO18, DE=GPIO21; baud 115200, 8N1.
- No dynamic allocation in the RX/TX path (fixed-size buffers only).
- Serial/Telnet logging is the debug surface; keep it but gate verbose per-frame dumps behind a runtime `raw` toggle.
- Device is inside the enclosure: everything testable over Telnet/OTA, no physical access.
- Status payload offsets (bytes after the `FF AF 13` type), verified for this BP501: current temp `p2`, set temp `p20`, pumps `p11` (bits[1:0]=pump1 0/1/2, bits[3:2]=pump2), flags `p10` (0x20=heater, 0x08=light, 0x04=high range), circ `p13` bit 0x02.
- Command frames ride the **assigned channel** `<id>`: set temp = `<id> BF 20 <tempF>`; toggle = `<id> BF 11 <item> 00` (pump1=0x04, pump2=0x05, light=0x11).
- Handshake: `FE BF 00` (poll) → `FE BF 01 02 F1 73` (our request) → `FE BF 02 <id>` (assignment, id = first payload byte, cap 0x2F) → `<id> BF 03` (our ack) → serve `<id> BF 06` with a command or `<id> BF 07`.

---

### Task 0: Commit current baseline

**Files:**
- Modify: (none — commits existing working-tree changes)

- [ ] **Step 1: Stage the D1 decoder fixes + the D2 spec**

```bash
cd /Users/jeremy/code/hottub
git add firmware/d1_sniffer/d1_sniffer.ino docs/balboa-protocol.md docs/superpowers/
```

- [ ] **Step 2: Commit**

```bash
git commit -m "d1_sniffer: pin verified BP501 status offsets; add D2 spec+plan

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 1: Scaffold `firmware/hottub/` (compiles, read-only base)

Stand up a buildable sketch: pinned profile, reused framing header, WiFi/OTA/Telnet, and a `make test` target. Behaviour is read-only (prints valid frames), identical in spirit to the sniffer.

**Files:**
- Create: `firmware/hottub/sketch.yaml`
- Create: `firmware/hottub/secrets.h.example`
- Create: `firmware/hottub/balboa_frame.h` (copied from sniffer, unchanged)
- Create: `firmware/hottub/hottub.ino`
- Modify: `firmware/hottub/Makefile` (switch to `--profile`, add `test` target)
- Modify: `firmware/hottub/.gitignore` or root `.gitignore` (ignore `secrets.h`, `build/`)

**Interfaces:**
- Produces: a compiling `firmware/hottub` sketch and `make test` target used by all later tasks.

- [ ] **Step 1: Copy the tested framing header**

```bash
cd /Users/jeremy/code/hottub/firmware/hottub
cp ../d1_sniffer/balboa_frame.h ./balboa_frame.h
mkdir -p test
cp ../d1_sniffer/test/test_balboa_frame.cpp ./test/test_balboa_frame.cpp
```

- [ ] **Step 2: Create `sketch.yaml`** (mirror the sniffer profile, no HomeSpan)

```yaml
profiles:
  hottub:
    notes: Waveshare ESP32-S3-RS485-CAN, 8MB flash, USB-CDC, dual-OTA partitions
    fqbn: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB
    platforms:
      - platform: esp32:esp32 (3.3.10)
        platform_index_url: https://espressif.github.io/arduino-esp32/package_esp32_index.json
    libraries:
      - WiFiManager (2.0.17)
      - TelnetStream (1.3.0)
      - NetApiHelpers (1.0.2)

default_profile: hottub
```

- [ ] **Step 3: Create `secrets.h.example`**

```cpp
#pragma once
// Copy to secrets.h (gitignored) and fill in before flashing.
#define OTA_PASSWORD "changeme"
```

- [ ] **Step 4: Create a minimal read-only `hottub.ino`**

```cpp
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

static constexpr const char* OTA_HOSTNAME = "hottub";
static constexpr int RS485_TX = 17, RS485_RX = 18, RS485_DE = 21;
static constexpr int RS485_BAUD = 115200;

struct DualPrint : public Print {
    size_t write(uint8_t c) override { Serial.write(c); TelnetStream.write(c); return 1; }
    size_t write(const uint8_t *b, size_t n) override { Serial.write(b, n); TelnetStream.write(b, n); return n; }
} Log;

static BalboaFrameReader reader;

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
```

- [ ] **Step 5: Update `Makefile`** — switch compile/upload to the profile and add a `test` target. Replace the `flash`, `ota`, and add `test`:

```make
PROFILE := hottub

flash:
	@test -n "$(PORT)" || { echo "\nERROR: No ESP32 port detected.\n"; exit 1; }
	$(ARDUINO_CLI) compile --profile $(PROFILE) .
	$(ARDUINO_CLI) upload -p $(PORT) --profile $(PROFILE) .

ota:
	$(ARDUINO_CLI) compile --profile $(PROFILE) --output-dir build/ .
	python3 $$(find ~/Library/Arduino15/packages/esp32 ~/.arduino15/packages/esp32 -name espota.py 2>/dev/null | head -1) \
	  -i $(OTA_HOST) -p $(OTA_PORT) --auth=$$(sed -n 's/.*OTA_PASSWORD[[:space:]]*"\(.*\)".*/\1/p' secrets.h) -f build/*.ino.bin

test:
	g++ -std=c++17 -Wall -o /tmp/ht_frame test/test_balboa_frame.cpp && /tmp/ht_frame
```

Also add `.PHONY: test` and update the `setup` target to `$(ARDUINO_CLI) compile --profile $(PROFILE) .`.

- [ ] **Step 6: Create `secrets.h` locally so it builds**

```bash
cp secrets.h.example secrets.h   # not committed
```

- [ ] **Step 7: Verify host test + firmware compile**

Run:
```bash
make test
arduino-cli compile --profile hottub .
```
Expected: `ALL TESTS PASSED`, then a successful compile ("Sketch uses …").

- [ ] **Step 8: Commit**

```bash
git add firmware/hottub/
git commit -m "hottub: scaffold read-only D2 base (profile, frame header, telnet, test target)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `balboa_build_frame()` TX helper

**Files:**
- Modify: `firmware/hottub/balboa_frame.h`
- Create: `firmware/hottub/test/test_build_frame.cpp`
- Modify: `firmware/hottub/Makefile` (`test` target)

**Interfaces:**
- Produces: `size_t balboa_build_frame(uint8_t* out, size_t outCap, const uint8_t* body, size_t bodyLen)` — writes `7E LEN <body> CRC 7E`, returns total length (0 if it won't fit). `body` = channel + type bytes + payload.

- [ ] **Step 1: Write the failing test** — `test/test_build_frame.cpp`

```cpp
#include "../balboa_frame.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    // Build the real new-client CTS frame and match the captured bytes.
    const uint8_t body[] = {0xFE, 0xBF, 0x00};
    const uint8_t want[] = {0x7E, 0x05, 0xFE, 0xBF, 0x00, 0xAC, 0x7E};
    uint8_t out[16];
    size_t n = balboa_build_frame(out, sizeof out, body, sizeof body);
    assert(n == sizeof want);
    assert(memcmp(out, want, n) == 0);

    // A built frame must pass our own validator and reader.
    assert(balboa_frame_valid(out, n));
    BalboaFrameReader r; int done = 0;
    for (size_t i = 0; i < n; i++) if (r.push(out[i])) done++;
    assert(done == 1);

    // Capacity guard.
    assert(balboa_build_frame(out, 4, body, sizeof body) == 0);

    printf("ALL TESTS PASSED\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -Wall -o /tmp/ht_bf test/test_build_frame.cpp && /tmp/ht_bf`
Expected: FAIL to compile — `balboa_build_frame` not declared.

- [ ] **Step 3: Implement in `balboa_frame.h`** (add above `struct BalboaFrameReader`)

```cpp
// Build 7E LEN <body> CRC 7E into out. body = channel + type bytes + payload.
// Returns total frame length, or 0 if it will not fit in outCap.
inline size_t balboa_build_frame(uint8_t* out, size_t outCap,
                                 const uint8_t* body, size_t bodyLen) {
    size_t total = bodyLen + 4;                 // 7E LEN <body> CRC 7E
    if (total > outCap) return 0;
    out[0] = BALBOA_FLAG;
    out[1] = (uint8_t)(bodyLen + 2);            // LEN = LEN-byte + body + CRC
    for (size_t i = 0; i < bodyLen; i++) out[2 + i] = body[i];
    out[2 + bodyLen] = balboa_crc8(out + 1, bodyLen + 1);   // CRC over LEN..last body
    out[3 + bodyLen] = BALBOA_FLAG;
    return total;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -Wall -o /tmp/ht_bf test/test_build_frame.cpp && /tmp/ht_bf`
Expected: `ALL TESTS PASSED`

- [ ] **Step 5: Add to `make test`** — append to the `test` target:

```make
	g++ -std=c++17 -Wall -o /tmp/ht_bf test/test_build_frame.cpp && /tmp/ht_bf
```

- [ ] **Step 6: Commit**

```bash
git add firmware/hottub/balboa_frame.h firmware/hottub/test/test_build_frame.cpp firmware/hottub/Makefile
git commit -m "hottub: add balboa_build_frame TX helper (host-tested)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `SpaState` + `parseStatus()`

**Files:**
- Create: `firmware/hottub/spa_state.h`
- Create: `firmware/hottub/test/test_spa_state.cpp`
- Modify: `firmware/hottub/Makefile` (`test` target)

**Interfaces:**
- Produces: `struct SpaState { uint8_t currentTempF, setTempF, pump1; bool pump2, light, circ, heating, highRange, tempKnown; uint32_t lastUpdateMs; bool stale; }` and `bool parseStatus(const uint8_t* payload, size_t len, SpaState& out, uint32_t nowMs)`. `payload` = bytes after the `FF AF 13` type.

- [ ] **Step 1: Write the failing test** — `test/test_spa_state.cpp`

```cpp
#include "../spa_state.h"
#include <cassert>
#include <cstdio>

// Payloads = bytes AFTER FF AF 13, taken from real BP501 captures.
// 95F cur / 85F set, everything off, high range:
static const uint8_t idle[] = {
    0x00,0x03,0x5F,0x0C,0x29,0x00,0x28,0x55,0x00,0x00,0x04,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x02,0x02,0x55,0x00,0x00,0x02,0x3C,0x00,0x00};
// cur unknown (0xFF) / 84F set, pump1 high + pump2 on, light on:
static const uint8_t active[] = {
    0x00,0x03,0xFF,0x01,0x1E,0x00,0x28,0x54,0x00,0x00,0x0C,0x0A,0x00,0x00,
    0x00,0x00,0x00,0x00,0x02,0x02,0x54,0x00,0x00,0x02,0x1E,0x00,0x00};

int main() {
    SpaState s;
    assert(parseStatus(idle, sizeof idle, s, 1000));
    assert(s.tempKnown && s.currentTempF == 95);
    assert(s.setTempF == 85);
    assert(s.pump1 == 0 && !s.pump2 && !s.light && !s.heating && s.highRange && !s.circ);
    assert(!s.stale && s.lastUpdateMs == 1000);

    assert(parseStatus(active, sizeof active, s, 2000));
    assert(!s.tempKnown && s.currentTempF == 95);   // holds last known
    assert(s.setTempF == 84);
    assert(s.pump1 == 2 && s.pump2 && s.light && s.highRange);

    assert(!parseStatus(idle, 10, s, 3000));         // too short -> false

    printf("ALL TESTS PASSED\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -Wall -o /tmp/ht_st test/test_spa_state.cpp && /tmp/ht_st`
Expected: FAIL to compile — `spa_state.h` missing.

- [ ] **Step 3: Implement `spa_state.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

struct SpaState {
    uint8_t  currentTempF = 0;   // valid only if tempKnown
    uint8_t  setTempF     = 0;
    uint8_t  pump1        = 0;   // 0=off, 1=low, 2=high
    bool     pump2        = false;
    bool     light        = false;
    bool     circ         = false;
    bool     heating      = false;
    bool     highRange    = false;
    bool     tempKnown    = false;
    uint32_t lastUpdateMs = 0;
    bool     stale        = true;
};

// payload = bytes AFTER the FF AF 13 message type. Offsets verified for BP501
// (see docs/balboa-protocol.md). Returns false if the payload is too short.
inline bool parseStatus(const uint8_t* p, size_t len, SpaState& s, uint32_t nowMs) {
    if (len < 21) return false;
    if (p[2] != 0xFF) { s.currentTempF = p[2]; s.tempKnown = true; }
    else               { s.tempKnown = false; }   // hold last currentTempF
    s.setTempF   = p[20];
    s.pump1      = p[11] & 0x03;
    s.pump2      = ((p[11] >> 2) & 0x03) != 0;
    s.light      = p[10] & 0x08;
    s.highRange  = p[10] & 0x04;
    s.heating    = p[10] & 0x20;
    s.circ       = p[13] & 0x02;
    s.lastUpdateMs = nowMs;
    s.stale = false;
    return true;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -Wall -o /tmp/ht_st test/test_spa_state.cpp && /tmp/ht_st`
Expected: `ALL TESTS PASSED`

- [ ] **Step 5: Add to `make test`**

```make
	g++ -std=c++17 -Wall -o /tmp/ht_st test/test_spa_state.cpp && /tmp/ht_st
```

- [ ] **Step 6: Commit**

```bash
git add firmware/hottub/spa_state.h firmware/hottub/test/test_spa_state.cpp firmware/hottub/Makefile
git commit -m "hottub: add SpaState + parseStatus (verified BP501 offsets, host-tested)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: `SpaProtocol` — command queue, state ingest, encoding

Build the pure brain's data side: status ingest, the command queue, and command-frame encoding. Registration/`pollTx` comes in Task 5.

**Files:**
- Create: `firmware/hottub/spa_control.h`
- Create: `firmware/hottub/spa_control.cpp`
- Create: `firmware/hottub/test/test_spa_control.cpp`
- Modify: `firmware/hottub/Makefile` (`test` target)

**Interfaces:**
- Produces: `class SpaProtocol` with `enum RegState {Unregistered,Requesting,Assigned}`; `void setArmed(bool)`, `bool armed()`, `RegState regState()`, `uint8_t channel()`, `const SpaState& state()`, `bool hasPendingCommand()`; command API `cmdSetTemp(uint8_t)`, `cmdTogglePump1()`, `cmdTogglePump2()`, `cmdToggleLight()`; `void onFrame(const uint8_t*, size_t, uint32_t)`; `size_t pollTx(uint8_t*, size_t)`.
- Consumes: `balboa_build_frame` (Task 2), `parseStatus`/`SpaState` (Task 3).

- [ ] **Step 1: Write the failing test** — `test/test_spa_control.cpp`

```cpp
#include "../spa_control.h"
#include "../balboa_frame.h"
#include <cassert>
#include <cstdio>

// Wrap a body in a full CRC'd frame for feeding onFrame().
static size_t frame(uint8_t* out, const uint8_t* body, size_t n) {
    return balboa_build_frame(out, 32, body, n);
}

int main() {
    SpaProtocol p;
    uint8_t buf[32];

    // Status frames are parsed regardless of arm state.
    const uint8_t status[] = {0xFF,0xAF,0x13,
        0x00,0x03,0x5F,0x0C,0x29,0x00,0x28,0x55,0x00,0x00,0x04,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x02,0x02,0x55,0x00,0x00,0x02,0x3C,0x00,0x00};
    size_t n = frame(buf, status, sizeof status);
    p.onFrame(buf, n, 1000);
    assert(p.state().currentTempF == 95 && p.state().setTempF == 85);

    // Command queue accepts commands.
    assert(!p.hasPendingCommand());
    p.cmdSetTemp(102);
    assert(p.hasPendingCommand());

    printf("ALL TESTS PASSED\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -Wall -o /tmp/ht_pc test/test_spa_control.cpp spa_control.cpp && /tmp/ht_pc`
Expected: FAIL to compile — `spa_control.h` missing.

- [ ] **Step 3: Implement `spa_control.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "spa_state.h"

class SpaProtocol {
public:
    enum RegState { Unregistered, Requesting, Assigned };

    void setArmed(bool a);
    bool armed() const { return armed_; }
    RegState regState() const { return reg_; }
    uint8_t channel() const { return channel_; }
    const SpaState& state() const { return state_; }
    bool hasPendingCommand() const { return qCount_ > 0; }

    void cmdSetTemp(uint8_t tempF);
    void cmdTogglePump1();
    void cmdTogglePump2();
    void cmdToggleLight();

    void onFrame(const uint8_t* frame, size_t len, uint32_t nowMs);
    size_t pollTx(uint8_t* out, size_t outCap);

private:
    enum Owe { OweNone, OweRequest, OweAck, OweCommand, OweNothing };
    struct Cmd { uint8_t type; uint8_t arg; };   // type 0x20=set temp, 0x11=toggle

    static const size_t QCAP = 8;
    bool     armed_   = false;
    RegState reg_     = Unregistered;
    uint8_t  channel_ = 0;
    Owe      owe_     = OweNone;
    SpaState state_;
    Cmd      queue_[QCAP];
    size_t   qHead_ = 0, qCount_ = 0;

    void enqueue(uint8_t type, uint8_t arg);
    size_t encodeCommand(const Cmd& c, uint8_t* out, size_t cap);
};
```

- [ ] **Step 4: Implement `spa_control.cpp`** (Task 4 portion — Task 5 fills `onFrame`/`pollTx` registration)

```cpp
#include "spa_control.h"
#include "balboa_frame.h"

static const uint8_t CMD_SET_TEMP = 0x20;
static const uint8_t CMD_TOGGLE   = 0x11;
static const uint8_t ITEM_PUMP1   = 0x04;
static const uint8_t ITEM_PUMP2   = 0x05;
static const uint8_t ITEM_LIGHT   = 0x11;

void SpaProtocol::setArmed(bool a) {
    armed_ = a;
    if (!a) { reg_ = Unregistered; channel_ = 0; owe_ = OweNone; }
}

void SpaProtocol::enqueue(uint8_t type, uint8_t arg) {
    if (qCount_ >= QCAP) return;
    queue_[(qHead_ + qCount_) % QCAP] = {type, arg};
    qCount_++;
}
void SpaProtocol::cmdSetTemp(uint8_t t)   { enqueue(CMD_SET_TEMP, t); }
void SpaProtocol::cmdTogglePump1()        { enqueue(CMD_TOGGLE, ITEM_PUMP1); }
void SpaProtocol::cmdTogglePump2()        { enqueue(CMD_TOGGLE, ITEM_PUMP2); }
void SpaProtocol::cmdToggleLight()        { enqueue(CMD_TOGGLE, ITEM_LIGHT); }

size_t SpaProtocol::encodeCommand(const Cmd& c, uint8_t* out, size_t cap) {
    if (c.type == CMD_SET_TEMP) {
        uint8_t body[] = {channel_, 0xBF, 0x20, c.arg};
        return balboa_build_frame(out, cap, body, sizeof body);
    }
    uint8_t body[] = {channel_, 0xBF, 0x11, c.arg, 0x00};
    return balboa_build_frame(out, cap, body, sizeof body);
}

void SpaProtocol::onFrame(const uint8_t* f, size_t len, uint32_t nowMs) {
    if (len < 5) return;
    uint8_t ch = f[2], t0 = f[3], t1 = f[4];
    const uint8_t* payload = f + 5;
    size_t plen = (len >= 7) ? len - 7 : 0;
    if (ch == 0xFF && t0 == 0xAF && t1 == 0x13) {   // status broadcast
        parseStatus(payload, plen, state_, nowMs);
        return;
    }
    // Registration handshake added in Task 5.
    (void)payload; (void)plen;
}

size_t SpaProtocol::pollTx(uint8_t* out, size_t cap) {
    (void)out; (void)cap;
    return 0;   // Task 5 implements the owed-frame responses
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `g++ -std=c++17 -Wall -o /tmp/ht_pc test/test_spa_control.cpp spa_control.cpp && /tmp/ht_pc`
Expected: `ALL TESTS PASSED`

- [ ] **Step 6: Add to `make test`**

```make
	g++ -std=c++17 -Wall -o /tmp/ht_pc test/test_spa_control.cpp spa_control.cpp && /tmp/ht_pc
```

- [ ] **Step 7: Commit**

```bash
git add firmware/hottub/spa_control.h firmware/hottub/spa_control.cpp firmware/hottub/test/test_spa_control.cpp firmware/hottub/Makefile
git commit -m "hottub: SpaProtocol skeleton — status ingest, command queue, encoding

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: `SpaProtocol` — registration state machine + `pollTx`

**Files:**
- Modify: `firmware/hottub/spa_control.cpp` (`onFrame`, `pollTx`)
- Modify: `firmware/hottub/test/test_spa_control.cpp` (add handshake tests)

**Interfaces:**
- Produces: full behaviour of `onFrame`/`pollTx` per the handshake in Global Constraints. `pollTx` returns the frame owed for the just-received poll (0 if none) and clears the owe flag.

- [ ] **Step 1: Add failing handshake tests** — append inside `main()` in `test/test_spa_control.cpp`, before the final print:

```cpp
    // --- Registration handshake (only when armed) ---
    SpaProtocol q;
    uint8_t tx[32];

    // Disarmed: new-client poll produces nothing.
    const uint8_t newclient[] = {0xFE,0xBF,0x00};
    size_t m = frame(buf, newclient, sizeof newclient);
    q.onFrame(buf, m, 10);
    assert(q.pollTx(tx, sizeof tx) == 0);
    assert(q.regState() == SpaProtocol::Unregistered);

    // Arm, then handshake.
    q.setArmed(true);
    q.onFrame(buf, m, 20);                         // FE BF 00
    size_t rn = q.pollTx(tx, sizeof tx);
    const uint8_t wantReq[] = {0xFE,0xBF,0x01,0x02,0xF1,0x73};
    uint8_t reqFrame[32]; size_t reqLen = balboa_build_frame(reqFrame, 32, wantReq, sizeof wantReq);
    assert(rn == reqLen && memcmp(tx, reqFrame, rn) == 0);
    assert(q.regState() == SpaProtocol::Requesting);

    // Assignment -> ack, channel stored.
    const uint8_t assign[] = {0xFE,0xBF,0x02,0x05};
    m = frame(buf, assign, sizeof assign);
    q.onFrame(buf, m, 30);
    size_t an = q.pollTx(tx, sizeof tx);
    const uint8_t wantAck[] = {0x05,0xBF,0x03};
    uint8_t ackFrame[32]; size_t ackLen = balboa_build_frame(ackFrame, 32, wantAck, sizeof wantAck);
    assert(an == ackLen && memcmp(tx, ackFrame, an) == 0);
    assert(q.regState() == SpaProtocol::Assigned && q.channel() == 0x05);

    // CTS with empty queue -> nothing-to-send.
    const uint8_t cts[] = {0x05,0xBF,0x06};
    m = frame(buf, cts, sizeof cts);
    q.onFrame(buf, m, 40);
    size_t nn = q.pollTx(tx, sizeof tx);
    const uint8_t wantNts[] = {0x05,0xBF,0x07};
    uint8_t ntsFrame[32]; size_t ntsLen = balboa_build_frame(ntsFrame, 32, wantNts, sizeof wantNts);
    assert(nn == ntsLen && memcmp(tx, ntsFrame, nn) == 0);

    // Queue a set-temp, next CTS sends it.
    q.cmdSetTemp(102);
    q.onFrame(buf, m, 50);
    size_t cn = q.pollTx(tx, sizeof tx);
    const uint8_t wantCmd[] = {0x05,0xBF,0x20,102};
    uint8_t cmdFrame[32]; size_t cmdLen = balboa_build_frame(cmdFrame, 32, wantCmd, sizeof wantCmd);
    assert(cn == cmdLen && memcmp(tx, cmdFrame, cn) == 0);
    assert(!q.hasPendingCommand());

    // Disarm resets registration.
    q.setArmed(false);
    assert(q.regState() == SpaProtocol::Unregistered && q.channel() == 0);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -Wall -o /tmp/ht_pc test/test_spa_control.cpp spa_control.cpp && /tmp/ht_pc`
Expected: FAIL — assertion on the request frame (pollTx still returns 0).

- [ ] **Step 3: Replace `onFrame` registration section** in `spa_control.cpp` — swap the `// Registration handshake added in Task 5.` block for:

```cpp
    if (!armed_) { owe_ = OweNone; return; }
    if (t0 != 0xBF) return;                          // only BF arbitration frames
    if (reg_ == Unregistered && ch == 0xFE && t1 == 0x00) {
        reg_ = Requesting; owe_ = OweRequest; return;
    }
    if (reg_ == Requesting && ch == 0xFE && t1 == 0x02 && plen >= 1) {
        channel_ = payload[0] > 0x2F ? 0x2F : payload[0];
        reg_ = Assigned; owe_ = OweAck; return;
    }
    if (reg_ == Assigned && ch == channel_ && t1 == 0x06) {
        owe_ = (qCount_ > 0) ? OweCommand : OweNothing; return;
    }
    owe_ = OweNone;
```

- [ ] **Step 4: Replace `pollTx`** in `spa_control.cpp`:

```cpp
size_t SpaProtocol::pollTx(uint8_t* out, size_t cap) {
    Owe o = owe_;
    owe_ = OweNone;
    switch (o) {
        case OweRequest: {
            uint8_t body[] = {0xFE, 0xBF, 0x01, 0x02, 0xF1, 0x73};
            return balboa_build_frame(out, cap, body, sizeof body);
        }
        case OweAck: {
            uint8_t body[] = {channel_, 0xBF, 0x03};
            return balboa_build_frame(out, cap, body, sizeof body);
        }
        case OweNothing: {
            uint8_t body[] = {channel_, 0xBF, 0x07};
            return balboa_build_frame(out, cap, body, sizeof body);
        }
        case OweCommand: {
            Cmd c = queue_[qHead_];
            qHead_ = (qHead_ + 1) % QCAP;
            qCount_--;
            return encodeCommand(c, out, cap);
        }
        default:
            return 0;
    }
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `g++ -std=c++17 -Wall -o /tmp/ht_pc test/test_spa_control.cpp spa_control.cpp && /tmp/ht_pc`
Expected: `ALL TESTS PASSED`

- [ ] **Step 6: Commit**

```bash
git add firmware/hottub/spa_control.cpp firmware/hottub/test/test_spa_control.cpp
git commit -m "hottub: SpaProtocol registration handshake + pollTx (host-tested)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: `BalboaBus` — ESP32 UART + DE half-duplex glue

**Files:**
- Create: `firmware/hottub/balboa_bus.h`
- Create: `firmware/hottub/balboa_bus.cpp`

**Interfaces:**
- Produces: `class BalboaBus { void begin(int rx, int tx, int de, uint32_t baud); void poll(uint32_t nowMs); SpaProtocol proto; }`. `poll()` drains UART1 → reader → `proto.onFrame` → `proto.pollTx` → transmits with DE control.
- Consumes: `BalboaFrameReader` (Task 1), `SpaProtocol` (Tasks 4–5).

- [ ] **Step 1: Implement `balboa_bus.h`**

```cpp
#pragma once
#include <Arduino.h>
#include "balboa_frame.h"
#include "spa_control.h"

class BalboaBus {
public:
    void begin(int rxPin, int txPin, int dePin, uint32_t baud);
    void poll(uint32_t nowMs);
    SpaProtocol proto;
private:
    int dePin_ = -1;
    BalboaFrameReader reader_;
    void transmit(const uint8_t* frame, size_t n);
};
```

- [ ] **Step 2: Implement `balboa_bus.cpp`**

```cpp
#include "balboa_bus.h"

void BalboaBus::begin(int rx, int tx, int de, uint32_t baud) {
    dePin_ = de;
    pinMode(dePin_, OUTPUT);
    digitalWrite(dePin_, LOW);                      // receive
    Serial1.begin(baud, SERIAL_8N1, rx, tx);
}

void BalboaBus::transmit(const uint8_t* frame, size_t n) {
    digitalWrite(dePin_, HIGH);                     // drive the bus
    Serial1.write(frame, n);
    Serial1.flush();                                // block until fully shifted out
    digitalWrite(dePin_, LOW);                      // release to receive
}

void BalboaBus::poll(uint32_t nowMs) {
    while (Serial1.available()) {
        if (reader_.push((uint8_t)Serial1.read())) {
            proto.onFrame(reader_.buf, reader_.len, nowMs);
            uint8_t out[16];
            size_t n = proto.pollTx(out, sizeof out);
            if (n) transmit(out, n);
        }
    }
}
```

- [ ] **Step 3: Verify it compiles inside the sketch** — temporarily add `#include "balboa_bus.h"` and a `static BalboaBus bus;` global to `hottub.ino` (Task 7 wires it fully), then:

Run: `arduino-cli compile --profile hottub .`
Expected: successful compile.

- [ ] **Step 4: Commit**

```bash
git add firmware/hottub/balboa_bus.h firmware/hottub/balboa_bus.cpp firmware/hottub/hottub.ino
git commit -m "hottub: BalboaBus UART1 + DE half-duplex I/O glue

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: `hottub.ino` — arm gate, Telnet command shell, integration

**Files:**
- Modify: `firmware/hottub/hottub.ino`

**Interfaces:**
- Consumes: `BalboaBus` (Task 6), `SpaProtocol` (Tasks 4–5).

- [ ] **Step 1: Replace `hottub.ino`** with the integrated version

```cpp
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

static void printState() {
    const SpaState& s = bus.proto.state();
    Log.printf("[spa] cur=%s set=%uF pump1=%u pump2=%s light=%s circ=%s heat=%s range=%s  reg=%d ch=0x%02X armed=%d\n",
        s.tempKnown ? String(s.currentTempF).c_str() : "--",
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
        bus.proto.cmdSetTemp((uint8_t)atoi(cmd + 5)); armedAt = millis(); Log.println("[queued] set temp");
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
    Log.printf("\n=== HotTub controller (D2) ===\nWiFi %s (%s)\nRead-only until `arm`.\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void loop() {
    ArduinoOTA.handle();
    pollTelnet();
    bus.poll(millis());

    // Auto-disarm after inactivity.
    if (bus.proto.armed() && millis() - armedAt > ARM_TIMEOUT_MS) {
        bus.proto.setArmed(false);
        Log.println("[disarmed] idle timeout");
    }

    // Print state on change.
    const SpaState& s = bus.proto.state();
    if (stateChanged(s, lastPrinted)) { printState(); lastPrinted = s; }
}
```

- [ ] **Step 2: Verify it compiles**

Run: `arduino-cli compile --profile hottub .`
Expected: successful compile.

- [ ] **Step 3: Run the full host test suite** (nothing regressed)

Run: `make test`
Expected: every `ALL TESTS PASSED`.

- [ ] **Step 4: Commit**

```bash
git add firmware/hottub/hottub.ino
git commit -m "hottub: arm gate + telnet command shell, full D2 integration

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 5: On-hardware verification (manual, over telnet)** — record results in the PR/commit notes:
  1. `make ota` to flash; `telnet hottub.local`.
  2. Confirm `[spa]` lines stream with correct `cur`/`set`/pump/light matching the panel (read-only path).
  3. `arm` → within a few seconds `reg=2` (Assigned) with a non-zero `ch` (registration succeeded). If it never assigns, capture frames with `raw` on and revisit Task 5 offsets.
  4. `temp 100` → observe `set=100F` in a subsequent `[spa]` line.
  5. `pump1` → observe `pump1` cycle; `pump2` and `light` → observe toggles. This satisfies the D2 success criterion.

---

### Task 8: Documentation corrections

**Files:**
- Modify: `docs/balboa-protocol.md` (Commands section)
- Modify: `docs/PLAN.md` (D2 note)

- [ ] **Step 1: Fix `docs/balboa-protocol.md` Commands section** — replace the claim that commands use a fixed `0x0A 0xBF` prefix with the verified behaviour: commands are sent on the client's **assigned channel** `<id>` (`<id> BF 20 <temp>` for set temp; `<id> BF 11 <item> 00` for toggles), obtained via the `FE BF 00/01/02` + `<id> BF 03` handshake. Keep the item-code table (pump1=0x04, pump2=0x05, light=0x11).

- [ ] **Step 2: Add a note to `docs/PLAN.md` D2 section** — the Balboa layer is our own tested code (built on `balboa_frame.h`), not MHotchin/BalBoaSpa; verification is via a Telnet command shell behind a runtime arm gate.

- [ ] **Step 3: Commit**

```bash
git add docs/balboa-protocol.md docs/PLAN.md
git commit -m "docs: correct Balboa command framing; note D2 uses our own library

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- Module structure → Tasks 1,2,3,4,5,6,7 (all files created). ✓
- SpaState fields/offsets → Task 3. ✓
- Bus registration handshake → Task 5. ✓
- Command semantics (set temp abs, toggles) → Tasks 4,5. ✓
- TX timing/DE + no-dynamic-alloc → Task 6 (fixed `out[16]`, stack bodies). ✓
- Arm gate + telnet shell → Task 7. ✓
- Error handling: CRC drop (reader), stale flag + 0xFF hold (Task 3), RX-only when unregistered/disarmed (Task 5). ✓ (2 s stale timer is a display concern; `stale` is set false on update — a periodic staleness check can be added in D3; noted, not required for D2 success criterion.)
- Host + on-hardware testing → each task's tests + Task 7 Step 5. ✓
- Doc corrections (protocol + PLAN) → Task 8. ✓

**Placeholder scan:** No TBD/TODO; every code step contains full code. On-hardware step is a manual checklist by necessity (hardware in enclosure), not a code placeholder.

**Type consistency:** `SpaProtocol` method names/signatures identical across Tasks 4–7; `balboa_build_frame` signature identical across Tasks 2,4,5,6; `SpaState` fields identical across Tasks 3,7. ✓

**Known deferrals (intentional, out of D2 scope):** light bit & F/C scale confirmation (needs clean captures), channel-assignment live confirmation (Task 7 Step 5), reconcile-to-desired-state helper (D3).
