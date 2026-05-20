# Hardware Reference

## Waveshare ESP32-S3-RS485-CAN

**Datasheet / Wiki:** https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN

### RS485 Pin Mapping

| Signal | GPIO | Notes |
|---|---|---|
| UART1 TX | GPIO17 | Data to bus |
| UART1 RX | GPIO18 | Data from bus |
| TX Enable | GPIO21 | HIGH = transmit, LOW = receive |

The board has an onboard RS485 transceiver (automatic or manual direction control). GPIO21 is the driver-enable pin — must be driven HIGH before writing and LOW before reading. The BalBoaSpa library handles this via a configurable DE pin.

### CAN Pins (unused for this project)

| Signal | GPIO |
|---|---|
| CAN TX | GPIO15 |
| CAN RX | GPIO16 |

### Power

| Source | Voltage | Notes |
|---|---|---|
| USB-C | 5V | Development / initial flash only |
| Screw terminal (VIN) | **7–36V** | Onboard DC-DC converter; 2A output at 3.3V |

The board has an onboard DC-DC switching regulator followed by an ME6217C33M5G LDO (800mA). The screw terminal VIN can accept the Balboa 12V supply directly — no intermediate regulator needed. All GPIOs run at 3.3V logic.

## Balboa BP501 Wiring

The Balboa system exposes an RS485 bus on the control panel connector. Typical pinout on the topside control panel cable:

| Wire | Signal |
|---|---|
| Black | GND |
| Red | +12V |
| White/Green/Yellow | RS485 A+ |
| White/Blue | RS485 B- |

Connect the Balboa 12V supply directly to power the board:
- Balboa `+12V` (Red) → Waveshare screw terminal `VIN`
- Balboa `GND` (Black) → Waveshare screw terminal `GND`
- Balboa RS485 A+ → Waveshare board `A+` terminal
- Balboa RS485 B- → Waveshare board `B-` terminal

This gives the ESP32 clean regulated power from the same supply that runs the spa — no separate power brick needed inside the enclosure.

The Waveshare board handles the TTL↔RS485 conversion internally.

## BP501 Notes

- The BP501 is a member of the Balboa BP5 series
- Uses the standard Balboa RS485 protocol at 115200 baud, 8N1
- Supports: 2 pumps (pump 1 is typically 2-speed), lights, blower, circ pump
- Temperature range: 80°F–104°F (26°C–40°C)
- Status messages broadcast every ~300ms unprompted
- Controller must join as a peripheral on the shared bus (client mode)
