# hottub

ESP32-based controller for a Balboa BP501 hot tub, with HomeKit integration and a custom iOS app.

## Hardware

**Waveshare ESP32-S3-RS485-CAN**
- RS485 UART1: TX=GPIO17, RX=GPIO18, TX-Enable=GPIO21
- Built-in WiFi antenna
- Installed inside the hot tub enclosure — all config and updates happen over WiFi

## Architecture

```
Balboa BP501 (RS485) ──► ESP32-S3 ──► HomeKit (direct, via HomeSpan)
                              │
                              ├── OTA firmware updates
                              ├── Web config portal (hottub.local)
                              └── iOS app (REST/WebSocket)
```

## Stack

| Layer | Choice |
|---|---|
| Firmware framework | Arduino (ESP32 Arduino Core) |
| HomeKit | [HomeSpan](https://github.com/HomeSpan/HomeSpan) |
| Balboa protocol | [MHotchin/BalBoaSpa](https://github.com/MHotchin/BalBoaSpa) (adapted) |
| WiFi provisioning | WiFiManager (captive portal) |
| iOS app | SwiftUI + HomeKit framework + local REST/WebSocket API |

## Project Structure

```
firmware/
  d1_sniffer/       # D1: Raw RS485 bus sniffer (bring-up / diagnostics)
  hottub/           # D2+: Main firmware (Balboa + HomeSpan + OTA + web portal)
ios/
  HotTub/           # Custom SwiftUI app
docs/
  PLAN.md           # Full implementation plan and deliverables
  hardware.md       # Board pinout, wiring, and hardware notes
  balboa-protocol.md  # BP501 RS485 protocol reference
```

## Deliverables

| # | Name | Description | Status |
|---|---|---|---|
| D1 | Hardware bring-up | Sniffer sketch, confirm RS485 comms | 🔄 In Progress |
| D2 | Balboa library | Full bidirectional control layer | Pending |
| D3 | HomeKit | HomeSpan integration, all spa services | Pending |
| D4 | Remote config & OTA | Web portal, OTA, WiFiManager | Pending |
| D5 | iOS app | SwiftUI app with live status + controls | Pending |
| D6 | Hardening | Watchdog, reconnect, logging, HTTPS | Pending |

## Getting Started

See [docs/PLAN.md](docs/PLAN.md) for the full implementation plan.
See [docs/hardware.md](docs/hardware.md) for wiring instructions.
