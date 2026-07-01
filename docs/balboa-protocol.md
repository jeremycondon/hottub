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

**Payload byte map** (bytes after the 2-byte type field, 0-indexed).

> ⚠️ The generic/published Balboa layout did **not** match our BP501 — the
> offsets below were pinned empirically by watching each byte change with the
> physical spa state (see raw captures at the end of this section). Re-validate
> if the spa firmware is ever updated.

| Byte | Field | Notes |
|---|---|---|
| 0, 1 | *unknown* | flip between (0,3) and (3,16) within ~1s — **not** the clock |
| 2 | currentTemp | °F raw; `0xFF` = no reading (no flow past sensor) — treat as unknown |
| 3, 4 | *unknown* | counters / heartbeat flags |
| 7 | setTemp (copy) | mirrors byte 20 |
| 10 | flags | bit `0x20` = heater active, bit `0x08` = light 1 (see note), bit `0x04` = temp range (1=high) |
| 11 | pumps | bits[1:0] = pump1 (0=off,1=low,2=high), bits[3:2] = pump2 (0=off, else on) |
| 13 | flags2 | bit `0x02` = circ pump running |
| 20 | setTemp | °F raw (canonical copy) |
| 24 | *unknown* | changes with mode; halved (60→30) during testing |

Heater / range / circ bits cross-checked against
[Dakoriki/ESPHome-Balboa-Spa](https://github.com/Dakoriki/ESPHome-Balboa-Spa/blob/main/spa_reader.h)
(that project runs in °C, so its temps are ÷2 — ours are raw °F).

**Still unconfirmed:** the **light** bit — our capture points to byte 10 bit `0x08`,
the reference project uses byte 14 == `0x03`; resolve with a clean single-toggle
capture. The **F/C scale** bit is unidentified (only ever observed in °F).
BP501 total status payload = 27 bytes.

Raw reference frames (BP501, `7E 20 FF AF 13 … CRC 7E`):

```
# current 95°F, set 85°F, all off:
… 00 03 5F 0C 29 00 28 55 00 00 04 00 00 00 00 00 00 00 02 02 55 00 00 02 3C 00 00 …
                ^cur(2)                 ^flags(10) all-off    ^set(20)
# current 94°F, set 84°F, pump1 high + pump2 on, light on:
… 00 03 FF 01 1E 00 28 54 00 00 0C 0A 00 …              54 …
                          ^flags 0x0C=light  ^pumps 0x0A         ^set 84
```

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
