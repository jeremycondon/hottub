# Implementation Plan

## Goals

- Control a Balboa BP501 hot tub from iOS via HomeKit (native Home app) and a custom SwiftUI app
- ESP32-S3 firmware on a Waveshare ESP32-S3-RS485-CAN board, connected via RS485
- Device installed inside the hot tub enclosure — zero physical access required for updates or config changes

## Stack

| Layer | Choice | Rationale |
|---|---|---|
| Firmware | Arduino + ESP32 Arduino Core 3.x | Best ecosystem for HomeSpan |
| HomeKit | HomeSpan | Mature, direct pairing (no hub), great OTA story, ESP32-S3 tested |
| Balboa protocol | Custom library (balboa_frame.h) | Tested bidirectional read/write; verified against BP501 reference implementation |
| WiFi provisioning | WiFiManager | Captive portal on first boot, credentials in NVS |
| OTA | ArduinoOTA | Built into ESP32 Arduino core, works alongside HomeSpan |
| iOS | SwiftUI + HomeKit + local REST/WebSocket | HomeKit for basic controls, custom API for spa-specific features |

## Deliverables

---

### D1 — Hardware Bring-Up & Protocol Sniffing

**Goal:** Confirm the Waveshare board can read valid Balboa RS485 frames from the BP501.

Tasks:
- Flash D1 sniffer sketch (`firmware/d1_sniffer/`)
- Connect RS485 A+/B- to the topside control panel port on the BP501
- Open serial monitor at 115200 baud
- Verify raw bytes arrive and frames decode correctly
- Annotate a sample status message (current temp, set temp, pump states)
- Note which features your specific BP501 exposes (pump 2? blower? aux?)

Success criteria: Decoded status messages streaming continuously with correct temperature readings.

---

### D2 — Balboa Control Library

**Goal:** Reliable bidirectional communication — read all spa state, send all control commands.

Tasks:
- Implement Balboa protocol library (`balboa_frame.h`) for Waveshare pin mapping (TX=17, RX=18, DE=21)
- Handle bus registration handshake, dynamic channel assignment, and polling
- Implement a `SpaState` struct with all relevant fields
- Wire up status update callbacks
- Implement and test each control command: set temp, pump 1 (2-speed), pump 2, lights, blower
- Handle edge cases: temp sensor uninitialized (0xFF), CRC errors, bus timeout

Success criteria: Can read current state and issue every control command, verified on actual hardware.

**Implementation note:** D2 uses a custom Balboa protocol library (`firmware/hottub/balboa_frame.h`), not MHotchin/BalBoaSpa. Verification is performed via a Telnet command shell (`firmware/hottub/hottub.ino`) available at runtime behind an `arm` gate, allowing inspection and manual command testing on actual hardware.

---

### D3 — HomeKit Integration (HomeSpan)

**Goal:** Spa appears in the iOS Home app with full control.

HomeKit service mapping:

| Balboa Feature | HomeKit Service | Notes |
|---|---|---|
| Water temp + set point | `Thermostat` | currentTemp, targetTemp, heatingState |
| Pump 1 | `Fan` | 2 speeds → 0/50/100% |
| Pump 2 | `Switch` | On/off |
| Light | `Lightbulb` | On/off |
| Blower | `Fan` | On/off |
| Economy mode | `Switch` | |
| Filter cycle | `Switch` (read-only indicator) | |

Tasks:
- Implement `SpanAccessory` wrapping D2 spa state
- Bidirectional sync: HomeKit commands → Balboa writes; Balboa status → HomeKit characteristic updates
- HomeKit pairing QR code on serial at boot
- Test all controls from Home app and Siri

Success criteria: Full control from native iOS Home app, state stays in sync.

---

### D4 — Remote Config & OTA

**Goal:** All config and firmware updates happen over WiFi. No physical access ever needed.

Tasks:
- **WiFiManager:** Captive portal on first boot (AP: `HotTub-Setup`). Credentials in NVS.
- **Fallback:** If WiFi fails 3 consecutive boots → relaunch AP mode automatically
- **mDNS:** Advertise as `hottub.local`
- **ArduinoOTA:** Firmware upload via Arduino IDE or `arduino-cli` over local network
- **Web portal** at `hottub.local`:
  - Current spa status (read-only dashboard)
  - Set temp limits, unit preference (F/C)
  - View recent log (ring buffer)
  - Trigger OTA update (upload .bin)
  - Factory reset option
- Password-protect the web portal

Success criteria: Can update firmware and change config from phone browser, no USB cable needed.

---

### D5 — iOS App

**Goal:** Custom SwiftUI app with richer UX than the Home app.

Architecture:
- HomeKit framework for temp / basic controls (stays in sync with Home app)
- Local REST API (`http://hottub.local/api/`) for spa-specific state and commands
- WebSocket (`ws://hottub.local/ws`) for real-time status push

ESP32 additions:
- `GET /api/status` — full JSON spa state
- `POST /api/command` — toggle pump, set temp, etc.
- WebSocket endpoint pushing state diffs on change

iOS app features:
- Live status dashboard (temp gauge, pump states, heating indicator)
- Temperature set point with presets (98°F / 102°F / 104°F)
- Scheduling: pre-heat timer (stored on ESP32, executed locally)
- Filter cycle status
- Economy mode toggle
- Firmware version + OTA trigger
- Connection status (local WiFi vs. unavailable)

Success criteria: App on device, golden path tested, submitted to TestFlight.

---

### D6 — Hardening

**Goal:** Unattended reliable operation.

Tasks:
- Hardware watchdog (ESP32 WDT) — resets if main loop stalls
- WiFi reconnect with exponential backoff (no reboot on transient disconnect)
- Heap monitoring — log warning if free heap < threshold
- NVS for all persistent config (survives OTA and power cycles)
- HTTPS for web portal (self-signed cert — avoids plaintext password on local network)
- Log ring buffer in PSRAM (last 500 events, accessible via web portal)
- Status LED: solid = connected, slow blink = connecting, fast blink = AP mode

Success criteria: 30-day unattended soak test with no manual intervention required.

---

## Build Order

```
Week 1    D1   Sniffer + hardware validation
Week 2    D2   Balboa library (read + all writes)
Week 3    D3   HomeSpan + HomeKit (working in Home app)
Week 4    D4   OTA + WiFiManager + web portal
Week 5    D3   HomeKit polish + all spa services
Week 6–8  D5   iOS app
Week 8    D6   Hardening pass
```

## Open Questions (resolved)

- HomeKit approach: **HomeSpan** (standalone, no hub) ✓
- Spa model: **BP501** ✓
- Features: **all** (temp primary) ✓
- iOS dev: **custom SwiftUI app** ✓
- Physical button: **not available**, AP-mode fallback is automatic ✓
