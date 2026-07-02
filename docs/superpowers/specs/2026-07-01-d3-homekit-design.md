# D3 — HomeKit Integration via HomeSpan (Design)

**Date:** 2026-07-01
**Deliverable:** D3 in `docs/PLAN.md`
**Status:** Approved design, pending implementation plan
**Builds on:** D2 (Balboa control library, hardware-verified)

## Goal

The spa appears in the iOS Home app (and Siri) with full control: temperature,
both pumps, and the light — state staying in sync whether changed from the app or
the physical topside panel.

Success criterion (PLAN.md): full control from the native iOS Home app, state
stays in sync.

## Context & decisions

- **Always-armed on boot.** D2's manual `arm` gate was a bring-up safety; HomeKit
  is autonomous, so D3 registers on the bus at startup and stays able to transmit.
  The telnet `disarm`/`arm` commands remain for debugging.
- **Four services:** Thermostat (temp), Pump 1 (Fan, 2-speed), Pump 2 (Switch),
  Light (Lightbulb). Blower/economy/filter are out of scope (not present on this
  BP501 or not yet decoded — economy is tracked in `TODO.md`).
- **Thermostat locked to Heat mode.** The spa is heat-only and the protocol has no
  clean heater on/off, so the target mode is fixed to Heat; an app request for
  Off/Cool is snapped back to Heat. Temperature setpoint is the real control.
- **Closed-loop reconciler with MAX=3.** HomeKit gives absolute desired states;
  the spa takes toggles. A bounded reconciler converges without spinning.
- **WiFi/OTA/Telnet unchanged.** Keep WiFiManager (provisioning), ArduinoOTA, and
  TelnetStream exactly as in D2; start HomeSpan on the already-established WiFi
  connection rather than letting it manage WiFi. This coexistence is D3's primary
  integration risk and is validated first on hardware.

The D2 layers (`balboa_frame.h`, `spa_state.h`, `spa_control.{h,cpp}`,
`balboa_bus.{h,cpp}`) are unchanged. D3 is additive.

## Module structure (`firmware/hottub/`)

Dependency direction: `hottub.ino` → `homekit_spa` → `spa_reconcile` → `spa_control`
(D2). `temp_convert.h` is a leaf helper.

| File | Responsibility | Testability |
|---|---|---|
| `temp_convert.h` | Pure °F↔°C helpers (HomeKit transmits Celsius internally). | host-tested |
| `spa_reconcile.{h,cpp}` | Pure `SpaReconciler`: per-actuator desired target + attempt counter; `tick(const SpaState&, SpaProtocol&)` drives one command per status frame toward each target, MAX=3 give-up. No Arduino deps. | host-tested |
| `homekit_spa.{h,cpp}` | HomeSpan `Service` subclasses (Thermostat, Fan, Switch, Lightbulb). HomeKit write → `reconciler.setX(target)`. `refresh(SpaState)` pushes actual values into characteristics. | compile-only |
| `hottub.ino` | Build the Accessory + services; register always-armed; loop runs `homeSpan.poll()` + `bus.poll()`; on each fresh status frame run `reconciler.tick()` then `homekit.refresh()`. | compile-only |
| `sketch.yaml` | Add HomeSpan (pinned) to the profile. | — |

## `temp_convert.h`

HomeKit always uses Celsius on the wire; the spa uses raw °F integers.

```
inline float fToC(uint8_t f);        // (f - 32) / 1.8
inline uint8_t cToF(float c);        // round(c * 1.8 + 32)
```

`cToF` rounds to the nearest integer °F for `cmdSetTemp`. Round-trip need not be
exact (HomeKit steps in 0.5 °C); the reconciler compares against the actual °F
setpoint the spa reports.

## `SpaReconciler`

Holds a target + counter for each of temp, pump1, pump2, light. A target is
"active" once HomeKit sets it and is cleared when reached or given up on.

```
class SpaReconciler {
public:
    void setTemp(uint8_t targetF);   // absolute
    void setPump1(uint8_t level);    // 0=off,1=low,2=high
    void setPump2(bool on);
    void setLight(bool on);

    // Called on each fresh status frame. Issues at most one command per active
    // target toward its goal via proto. Returns nothing; inspect proto/state.
    void tick(const SpaState& s, SpaProtocol& proto);

    // For give-up resync: which targets were abandoned this tick (so the HomeKit
    // layer can snap characteristics back to actual). Query helpers per actuator.
    bool tempGaveUp() const; ...
};
```

Per active target, on each `tick`:
- **reached** (actual == target) → clear target, reset counter to 0.
- **differs, counter < MAX (3)** → issue one command toward target, counter++.
  - temp: `proto.cmdSetTemp(targetF)` (absolute re-send).
  - pump1: one `proto.cmdTogglePump1()` — the cyclic off→low→high→off means the
    number of toggles to reach a level is `(target - current) mod 3`, issued one
    per tick until reached.
  - pump2/light: one toggle if actual differs.
- **differs, counter >= MAX** → give up: set a "gaveUp" flag for that actuator,
  clear the target. (The HomeKit layer resyncs the characteristic to actual and
  logs.)

Setting a new target (new HomeKit command) resets that actuator's counter to 0
and clears any gaveUp flag. One command per actuator per tick keeps the RS485
queue shallow and paces to the spa's ~300 ms status cadence.

## HomeKit service mapping (`homekit_spa`)

One Accessory (category Thermostat) with four services.

- **Thermostat**
  - `CurrentTemperature` = `fToC(state.currentTempF)`; when `!state.tempKnown`
    (spa reports 0xFF), hold the last known value.
  - `TargetTemperature` (°C) → write triggers `reconciler.setTemp(cToF(value))`.
    Valid range ≈ 26.5–40 °C (80–104 °F), 0.5 °C step.
  - `TargetHeatingCoolingState` fixed to **Heat**; a write of Off/Cool/Auto is
    forced back to Heat.
  - `CurrentHeatingCoolingState` = Heat when `state.heating` else Off.
  - `TemperatureDisplayUnits` = Fahrenheit (presentation only; wire stays °C).
- **Pump 1 → Fan**: `Active` + `RotationSpeed`. Write → level: `Active==0` → 0
  (off); else `RotationSpeed <= 50` → 1 (low), `> 50` → 2 (high) →
  `reconciler.setPump1(level)`. Refresh maps level → Active/RotationSpeed
  (off=0/0, low=1/50, high=1/100).
- **Pump 2 → Switch**: `On` → `reconciler.setPump2(on)`.
- **Light → Lightbulb**: `On` → `reconciler.setLight(on)`.
- Circ pump: not exposed.

`refresh(const SpaState&)` writes actual values into every characteristic (via
HomeSpan `setVal`) so the Home app tracks physical-panel changes and reconciler
give-ups.

## Main loop & integration

```
setup(): WiFiManager.autoConnect (as D2) → TelnetStream + ArduinoOTA (as D2)
         → bus.begin(...) → bus.proto.setArmed(true)   // always-armed
         → homeSpan.begin(Category::Thermostat, "HotTub")
         → build Accessory + 4 services (homekit_spa)
loop():  ArduinoOTA.handle(); pollTelnet(); homeSpan.poll(); bus.poll(millis());
         if (new status frame since last loop) {
             reconciler.tick(bus.proto.state(), bus.proto);
             homekit.refresh(bus.proto.state());   // + resync any gaveUp actuators
         }
```

"New status frame" is detected via `state.lastUpdateMs` changing. HomeSpan pairing
QR/setup code prints on Serial at boot.

**Integration risk — HomeSpan vs. our WiFi/OTA/Telnet stack.** HomeSpan can try to
own WiFi and ships its own OTA + serial CLI. We keep WiFiManager + ArduinoOTA +
TelnetStream and start HomeSpan on the existing connection. This is the first thing
verified on hardware; if HomeSpan cannot coexist, the fallback is to let HomeSpan
manage WiFi and defer captive-portal provisioning to D4 (documented, not built
here).

## Error handling

- Reconciler never spins: bounded by MAX=3 per target, paced one command per
  status frame; give-up logs and resyncs HomeKit to actual.
- `0xFF` current temp: hold last known (D2 behavior); HomeKit shows last value.
- Bus/WiFi drop: D2's reader/`stale` behavior unchanged; HomeSpan handles its own
  HomeKit-controller reconnect.
- Always-armed: if registration is lost, D2's self-heal re-requests on the next
  `FE BF 00`.

## Testing

**Host (native, extends the suite):**
- `temp_convert`: `fToC`/`cToF` round-trips and boundary values (80/104 °F).
- `SpaReconciler`: converges to each target in ≤2 toggles; exactly one command per
  tick; pump1 cyclic targeting (high→low needs 2 toggles); temp re-send; MAX=3
  give-up sets the gaveUp flag and clears the target; a new target resets the
  counter. Uses a real `SpaProtocol` and inspects its queued output.

**Compile-verified:** `homekit_spa`, `hottub.ino` (profile build with HomeSpan).

**On-hardware (human):** pair with the Home app via the serial QR/code; exercise
each service; change something on the physical panel and confirm the app updates;
confirm WiFiManager/ArduinoOTA/Telnet still function alongside HomeSpan.

## Out of scope (YAGNI)

- Blower, economy mode (tracked in `TODO.md`), filter-cycle indicator.
- Web portal / NVS config UI (D4).
- Multi-accessory bridging (single accessory suffices).

## Open items to resolve during implementation

1. Confirm HomeSpan coexists with WiFiManager + ArduinoOTA + Telnet on the existing
   WiFi connection (early on-hardware check).
2. Confirm the pinned HomeSpan version and that the `default_8MB` partition scheme
   provides the NVS space HomeSpan needs for pairing.
3. Wire `SpaState.stale` (tracked in `TODO.md`) if convenient during loop work.
