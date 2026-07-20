#pragma once
#include "HomeSpan.h"
#include "spa_state.h"
#include "spa_reconcile.h"
#include "temp_convert.h"

struct SpaThermostat : Service::Thermostat {
    SpanCharacteristic *curTemp, *tgtTemp, *curState, *tgtState, *units;
    SpaReconciler& rec;
    explicit SpaThermostat(SpaReconciler& r);
    boolean update() override;
    void refresh(const SpaState& s);
};

// NOTE (HomeSpan 2.1.8): Service::Fan requires Characteristic::Active (not
// ::On) as its on/off characteristic (HAP "Fan v2"). `on` below is bound to
// an Active characteristic; the field is still named `on` to keep the same
// shape as the brief's interface.
struct SpaPump1Fan : Service::Fan {
    SpanCharacteristic *on, *speed;
    SpaReconciler& rec;
    explicit SpaPump1Fan(SpaReconciler& r);
    boolean update() override;
    void refresh(const SpaState& s);
};

struct SpaPump2Switch : Service::Switch {
    SpanCharacteristic *on;
    SpaReconciler& rec;
    explicit SpaPump2Switch(SpaReconciler& r);
    boolean update() override;
    void refresh(const SpaState& s);
};

struct SpaLight : Service::LightBulb {
    SpanCharacteristic *on;
    SpaReconciler& rec;
    explicit SpaLight(SpaReconciler& r);
    boolean update() override;
    void refresh(const SpaState& s);
};

// Owns the four services; refresh() pushes actual state + clears give-up flags.
struct SpaHomeKit {
    SpaThermostat*  thermostat = nullptr;
    SpaPump1Fan*    pump1 = nullptr;
    SpaPump2Switch* pump2 = nullptr;
    SpaLight*       light = nullptr;
    SpaReconciler&  rec;
    explicit SpaHomeKit(SpaReconciler& r) : rec(r) {}
    void build();                     // call after homeSpan.begin()
    void refresh(const SpaState& s);
};
