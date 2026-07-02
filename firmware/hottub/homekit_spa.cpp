#include "homekit_spa.h"

// ---- Thermostat (locked to Heat) ----
SpaThermostat::SpaThermostat(SpaReconciler& r) : Service::Thermostat(), rec(r) {
    curTemp  = new Characteristic::CurrentTemperature(20);
    tgtTemp  = new Characteristic::TargetTemperature(38);
    tgtTemp->setRange(26.5, 40, 0.5);
    curState = new Characteristic::CurrentHeatingCoolingState(0);   // Off
    tgtState = new Characteristic::TargetHeatingCoolingState(1);    // Heat
    tgtState->setValidValues(1, 1);                                 // Heat only
    units    = new Characteristic::TemperatureDisplayUnits(1);      // Fahrenheit (display)
    new Characteristic::Name("Water");
}
boolean SpaThermostat::update() {
    if (tgtState->updated()) tgtState->setVal(1);                   // force Heat
    if (tgtTemp->updated())  rec.setTemp(cToF(tgtTemp->getNewVal<float>()));
    return true;
}
void SpaThermostat::refresh(const SpaState& s) {
    if (s.tempKnown) curTemp->setVal(fToC(s.currentTempF));         // else hold last
    tgtTemp->setVal(fToC(s.setTempF));
    curState->setVal(s.heating ? 1 : 0);
}

// ---- Pump 1 (Fan, 2-speed) ----
// HomeSpan 2.1.8's Service::Fan requires Characteristic::Active (HAP "Fan
// v2"); it does not accept Characteristic::On. `on` is bound to Active here.
SpaPump1Fan::SpaPump1Fan(SpaReconciler& r) : Service::Fan(), rec(r) {
    on    = new Characteristic::Active(0);
    speed = new Characteristic::RotationSpeed(0);
    speed->setRange(0, 100, 50);                                    // 0 / 50 / 100
    new Characteristic::Name("Pump 1");
}
boolean SpaPump1Fan::update() {
    bool o  = on->updated()    ? on->getNewVal()    : on->getVal();
    int sp  = speed->updated() ? speed->getNewVal() : speed->getVal();
    uint8_t level = !o ? 0 : (sp <= 50 ? 1 : 2);
    rec.setPump1(level);
    return true;
}
void SpaPump1Fan::refresh(const SpaState& s) {
    on->setVal(s.pump1 ? 1 : 0);
    speed->setVal(s.pump1 == 0 ? 0 : (s.pump1 == 1 ? 50 : 100));
}

// ---- Pump 2 (Switch) ----
SpaPump2Switch::SpaPump2Switch(SpaReconciler& r) : Service::Switch(), rec(r) {
    on = new Characteristic::On(0);
    new Characteristic::Name("Pump 2");
}
boolean SpaPump2Switch::update() {
    if (on->updated()) rec.setPump2(on->getNewVal());
    return true;
}
void SpaPump2Switch::refresh(const SpaState& s) { on->setVal(s.pump2 ? 1 : 0); }

// ---- Light ----
SpaLight::SpaLight(SpaReconciler& r) : Service::LightBulb(), rec(r) {
    on = new Characteristic::On(0);
    new Characteristic::Name("Light");
}
boolean SpaLight::update() {
    if (on->updated()) rec.setLight(on->getNewVal());
    return true;
}
void SpaLight::refresh(const SpaState& s) { on->setVal(s.light ? 1 : 0); }

// ---- Container ----
void SpaHomeKit::build() {
    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name("HotTub");
      thermostat = new SpaThermostat(rec);
      pump1      = new SpaPump1Fan(rec);
      pump2      = new SpaPump2Switch(rec);
      light      = new SpaLight(rec);
}
void SpaHomeKit::refresh(const SpaState& s) {
    thermostat->refresh(s);
    pump1->refresh(s);
    pump2->refresh(s);
    light->refresh(s);
    // Give-ups are already reflected (refresh pushes actual); clear the flags.
    if (rec.tempGaveUp() || rec.pump1GaveUp() || rec.pump2GaveUp() || rec.lightGaveUp())
        rec.clearGaveUp();
}
