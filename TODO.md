# TODO / Follow-ups

Tracked loose ends that are intentionally deferred — not blocking current work.

## HomeKit / D3

- **Eco (economy) mode** — Jeremy is interested in exposing Balboa "economy" mode
  as a HomeKit control. Not yet decoded: we haven't identified which status byte/bit
  reports economy state. Needs a capture that toggles economy on the topside panel to
  find the bit, then a decoder field + a HomeKit Switch + reconciler target.
  Deferred out of D3 scope.

## Decoder confirmations (from D1/D2)

- **Light status bit** — our capture points to status payload `p10` bit `0x08`; the
  ESPHome reference uses `p14 == 0x03`. Confirm with a clean single light-toggle
  capture (see `docs/balboa-protocol.md`).
- **F/C scale bit** — unidentified (only °F ever observed). Confirm with a °C capture
  if the tub is ever switched to Celsius.

## Robustness (deferred from D2 whole-branch review)

- **`SpaState.stale`** — never set true yet; wire a ">2s since last status" staleness
  flag. Natural pickup during D3 (the loop already tracks status arrival).
- **Channel-assignment out-of-range** — the assigned id is currently capped at `0x2F`;
  consider reject-and-log instead if the spa ever assigns a higher id.
