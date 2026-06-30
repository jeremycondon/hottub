# Balboa RS485 Protocol Reference

**Primary source:** https://github.com/ccutrer/balboa_worldwide_app/blob/main/doc/protocol.md  
**Reference implementation:** https://github.com/MHotchin/BalBoaSpa  
**Alt implementation (ESP32):** https://github.com/NorthernMan54/esp32_balboa_spa

## Physical Layer

- **Baud rate:** 115200
- **Format:** 8N1 (8 data bits, no parity, 1 stop bit)
- **Bus:** RS485 half-duplex, multi-drop
- **Voltage:** Differential, ±5V typical

## Frame Format

```
[0xFF] [LEN] [MSG_TYPE_HI] [MSG_TYPE_LO] [PAYLOAD...] [CRC] [0x7E]
```

| Byte | Value | Description |
|---|---|---|
| 0 | `0xFF` | Start of message |
| 1 | N | Length (includes type bytes and payload, excludes start/end/CRC) |
| 2–3 | varies | Message type (2 bytes) |
| 4..N+1 | varies | Payload |
| N+2 | CRC | CRC8 of bytes 1..N+1 |
| N+3 | `0x7E` | End of message |

### CRC Algorithm

CRC8, poly `0x07`, init `0x02`. See MHotchin/BalBoaSpa `crc.cpp` for reference.

## Key Message Types

### Status Update (spa → peripherals, ~300ms interval)

Type: `0xFF 0xAF`

Contains: current temp, set temp, pump 1/2/3 state, light state, heating state, temp scale (F/C), filter cycle, time of day, restriction mode.

**Payload byte map** (bytes after the 2-byte type field, 0-indexed):

| Byte | Field | Notes |
|---|---|---|
| 0 | hour | 0–23 |
| 1 | minute | 0–59 |
| 2 | flags | bit 0 = Celsius, bit 2 = high range, bit 4 = heating |
| 3 | currentTemp | °F raw, or °C × 2; `0xFF` = sensor not initialized |
| 4 | flags2 | |
| 5 | setTemp | same encoding as currentTemp |
| 6 | pumpFlags | bits[1:0] = pump1 (0=off,1=low,2=high), bits[3:2] = pump2 |
| 7 | miscFlags | bit 0 = light, bit 1 = circ pump |
| 8+ | additional flags | vary by firmware revision |

Exact offsets can vary slightly across BP5xx firmware versions — validate against observed frames.

### Commands (peripheral → spa)

Commands from peripherals use a common 2-byte type prefix `0x0A 0xBF`. The first byte of the payload is the command discriminator.

#### Set Temp

Type: `0x0A 0xBF`, command byte: `0x20`  
Payload (after command byte): 1 byte — desired temp in units matching current scale (°F raw, or °C × 2)

#### Toggle Request (pump, light, etc.)

Type: `0x0A 0xBF`, command byte: `0x11`  
Payload (after command byte): 1 byte — item code

| Code | Item |
|---|---|
| `0x04` | Pump 1 |
| `0x05` | Pump 2 |
| `0x06` | Pump 3 |
| `0x11` | Light 1 |
| `0x3C` | Blower |
| `0x51` | Aux 1 |

### Clear to Send / Bus Arbitration

The spa broadcasts a "Clear to Send" (CTS) frame periodically. Peripherals must wait for CTS before transmitting to avoid collisions. The BalBoaSpa library handles this automatically.

## BP501-Specific Notes

- Pump 1 is 2-speed (low/high); toggle cycles: off → low → high → off
- Pump 2 is 1-speed
- Light is on/off only (no dimming or color on base models)
- Circ pump runs automatically, not user-controllable via RS485
- `currentTemp` will read `0xFF` (255) if the temp sensor hasn't initialized yet — treat as "unknown"

## Bus Behavior

- The spa is the bus master and broadcasts status unprompted every ~300ms
- Peripherals (including our ESP32) are secondary devices that listen and occasionally transmit
- Always wait for CTS before sending — the BalBoaSpa library manages this
- Electrical noise from the heater contactor is common; expect occasional CRC errors, just discard and wait for the next frame
