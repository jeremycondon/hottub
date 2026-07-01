# D2 — Balboa Control Library + Telnet Harness (Design)

**Date:** 2026-07-01
**Deliverable:** D2 in `docs/PLAN.md`
**Status:** Approved design, pending implementation plan

## Goal

Reliable bidirectional communication with the Balboa BP501 over RS485: read all
spa state and issue every control command, verified on actual hardware. This is
the foundation D3 (HomeKit) and D5 (iOS app) build on.

Success criterion (from PLAN.md): can read current state and issue every control
command, verified on the real spa.

## Context & decisions

- **Build on our own tested code**, not MHotchin/BalBoaSpa. We already have a
  host-tested `balboa_frame.h` (frame reader + CRC) that decodes real frames from
  this specific spa, and a status byte map pinned empirically (see
  `docs/balboa-protocol.md`). This diverges from PLAN.md's original MHotchin
  choice; the plan doc should be updated to reflect it.
- **Telnet command shell** for verification (device lives in the enclosure, so
  testing must be remote). Reuses the sniffer's WiFi + OTA + Telnet scaffolding.
- **Runtime "arm" gate** around transmit. Boots read-only; TX (bus registration +
  commands) is enabled only after an explicit `arm` command and auto-disarms on
  an idle timeout. This is our first firmware that writes to the live bus.
- **No HomeKit in D2.** HomeSpan is added in D3, layered on `spa_control`.

## Module structure (`firmware/hottub/`)

Each unit has one purpose, a defined interface, and is testable in isolation.
Dependency direction: `hottub.ino` → `spa_control` → `balboa_bus` → `balboa_frame`;
`spa_state` is a shared data type used by `balboa_bus`/`spa_control`.

| File | Responsibility | Depends on |
|---|---|---|
| `balboa_frame.h` | Copied from sniffer: `BalboaFrameReader` (RX) + `balboa_crc8` + `balboa_frame_valid`. **Adds** `buildFrame()` TX helper that writes `7E LEN CH TYPE… PAYLOAD CRC 7E` into a caller-provided buffer. | — |
| `spa_state.h` | `SpaState` struct + `parseStatus(payload, len, SpaState&)` using the verified offsets. Tracks `tempKnown`, `lastUpdateMs`, `stale`. | `balboa_frame.h` |
| `balboa_bus.{h,cpp}` | RS485 half-duplex driver. RX pump feeds `BalboaFrameReader`. TX via DE pin. Bus-registration state machine. Single-slot command send on our channel's CTS. | `balboa_frame.h` |
| `spa_control.{h,cpp}` | High-level API: `setTemp(uint8_t f)`, `togglePump1()`, `togglePump2()`, `toggleLight()`. Encodes command frames and enqueues on the bus. Optional `reconcile*` helpers (issue N toggles toward a desired state) — used by D3, not required for D2. | `balboa_bus`, `spa_state` |
| `hottub.ino` | WiFi/OTA/Telnet setup (from sniffer) + command shell. Prints `SpaState` on change. | all of the above |
| `sketch.yaml` | Pin platform + WiFiManager + TelnetStream (+ NetApiHelpers). **No HomeSpan.** Mirror the sniffer profile. | — |
| `Makefile` | Already present; switch compile/upload targets to `--profile` (like the sniffer). | — |
| `test/` | Host (native) tests. Extends the sniffer's approach. | — |

## SpaState

```
struct SpaState {
    uint8_t currentTempF;   // valid only if tempKnown
    uint8_t setTempF;
    uint8_t pump1;          // 0=off, 1=low, 2=high
    bool    pump2;          // single-speed on/off
    bool    light;
    bool    circ;
    bool    heating;
    bool    highRange;
    bool    tempKnown;      // false when the spa reports 0xFF (no flow)
    uint32_t lastUpdateMs;  // millis() of last valid status frame
    bool    stale;          // true if no status for > STALE_MS
};
```

Verified offsets (payload = bytes after `FF AF 13`; see `docs/balboa-protocol.md`):
current temp `p2`, set temp `p20`, pumps `p11` (bits[1:0]=pump1, bits[3:2]=pump2),
flags `p10` (bit 0x20 heater, bit 0x08 light, bit 0x04 high range), circ `p13`
bit 0x02.

Two bits remain **unconfirmed** and carry a TODO until a clean capture lands:
the light bit (p10 bit 0x08 per our capture vs p14==0x03 per the reference
project) and the F/C scale bit (only °F ever observed — D2 assumes °F).

## Bus registration & transmit

Arbitration observed in our own capture:

- Existing topside panel at **channel 0x10**, polled by the spa with a
  Clear-To-Send: `7E 05 10 BF 06 <crc> 7E`.
- **New-client CTS** on channel **0xFE**: `7E 05 FE BF 00 <crc> 7E`.

Registration flow (only while **armed**), with concrete bytes confirmed against
the working [Dakoriki/ESPHome-Balboa-Spa](https://github.com/Dakoriki/ESPHome-Balboa-Spa/blob/main/spa_reader.h)
implementation (payloads shown without the `7E LEN … CRC 7E` wrapper):

1. Spa broadcasts new-client poll: `FE BF 00`.
2. We reply with a **new-client request**: `FE BF 01 02 F1 73`. `F1 73` is a
   device nonce (the reference hardcodes it; we may randomize to avoid collisions
   if two clients ever join at once).
3. Spa replies with a **channel assignment**: `FE BF 02 <id> …` — our assigned id
   is the **first payload byte**, capped at `0x2F`. Store it.
4. We **ack**: `<id> BF 03`.
5. Thereafter, when the spa polls **our** channel with CTS (`<id> BF 06`),
   transmit one queued command frame, or **nothing-to-send** `<id> BF 07` if the
   queue is empty. One command per poll cycle.

While **disarmed**: never register, never transmit — behaviour is identical to
today's read-only sniffer.

Note: the handshake message *roles* are now confirmed; remaining risk is only
whether our specific BP501 firmware assigns/accepts a channel identically. This
still gets dedicated on-hardware iteration, but it is no longer a blind guess.

## Command semantics

Commands are sent on **our assigned channel `<id>`** (confirmed via the reference
implementation), *not* a fixed `0x0A` prefix as our old `docs/balboa-protocol.md`
states — that doc's Commands section will be corrected.

- **Set temp (absolute):** `<id> BF 20 <temp>`, one payload byte = desired temp in
  the current scale (°F raw for us; the reference sends value×2 because it runs in
  °C).
- **Toggles (relative):** `<id> BF 11 <item> 00` — item code plus a trailing `00`
  pad. pump1=`0x04`, pump2=`0x05`, light=`0x11` (reference also lists jet3=`0x06`,
  range=`0x50`, heat-mode=`0x51`).

Pumps/light have no absolute "set" — only toggle. pump1 cycles off→low→high→off.
D2 exposes single toggles. A `reconcile(desired)` helper that reads `SpaState` and
issues the right number of toggles is provided for D3's use but is not part of the
D2 success criterion.

## TX timing & half-duplex

- DE = GPIO21: HIGH to transmit, LOW to receive.
- Sequence: set DE HIGH → `Serial1.write(frame, len)` → `Serial1.flush()` (blocks
  until the UART shift register empties) → DE LOW. Must complete inside the CTS
  slot (~10 ms).
- **No dynamic allocation** in the RX/TX path (per CLAUDE.md). Frame buffers are
  fixed-size stack/member arrays.

## Arm gate

- Boot: read-only. TX subsystem disabled.
- `arm` (telnet): enable registration + command TX. `disarm` or an idle timeout
  (`ARM_TIMEOUT_MS`, default 5 min) returns to read-only and forgets the channel.
- Every command shell action requiring TX checks the armed flag first.

## Telnet command shell

Line-oriented over the existing Telnet stream (and USB serial):

| Command | Effect |
|---|---|
| `status` | Print current `SpaState` |
| `arm` / `disarm` | Toggle the TX gate |
| `temp <F>` | Set target temperature (armed) |
| `pump1` / `pump2` / `light` | Send one toggle (armed) |
| `raw` | Toggle the per-frame hex/decode dump (calibration aid) |

`SpaState` is also printed automatically whenever a field changes.

## Error handling

- CRC failures dropped by the reader (existing behaviour).
- `SpaState.stale = true` if no valid status for `STALE_MS` (default 2 s).
- `currentTempF` holds last known value when the spa reports `0xFF`;
  `tempKnown=false`.
- If no channel is assigned after N new-client CTS cycles while armed, log and
  stay RX-only; re-`arm` retries.
- Command TX only attempted in our assigned CTS slot; never blind-transmit.

## Testing

**Host (native, `test/`), extends the sniffer suite:**
- CRC-8 known vectors.
- `buildFrame()` → `BalboaFrameReader` roundtrip (including payload bytes equal to
  `0x7E`).
- Command encoding golden bytes (set temp, each toggle).
- `parseStatus()` asserted against the real captured frames in
  `docs/balboa-protocol.md` (temp, set temp, pump states, light, circ, range).

**On-hardware (telnet), satisfies the D2 success criterion:**
- Status streams continuously with correct values.
- `arm` → registration succeeds (assigned a channel).
- Each command (`temp`, `pump1`, `pump2`, `light`) issued and the corresponding
  `SpaState` field observed to change.

## Out of scope (YAGNI)

- HomeKit / HomeSpan (D3).
- Web portal, NVS-persisted config, WiFi fallback/mDNS polish (D4).
- Blower / aux (not present in any observed BP501 frame; add if one appears).
- Absolute pump/light state API beyond the toggle-reconcile helper.

## Open items to resolve during implementation

1. Verify our BP501 assigns/accepts a channel via the (now-known) handshake byte
   sequence — the one piece that can only be confirmed live.
2. Confirm the light bit and F/C scale bit via clean single-toggle / °C captures.
3. Correct `docs/balboa-protocol.md` Commands section (commands go on the assigned
   channel `<id>`, not a fixed `0x0A` prefix; toggle frames carry a trailing `00`).
4. Update `docs/PLAN.md` to note the library is our own code, not MHotchin.
