#include <Arduino.h>
#include <sim/battery_sim.h>

bool BatterySim::setup() {
    NodeEntity::setup();   // base class first — sets the active device context (A2)
    _soc.setup(getNodeId(), "soc");
    _voltage.setup(getNodeId(), "voltage");
    _current.setup(getNodeId(), "current");
    _power.setup(getNodeId(), "power");
    _temperature.setup(getNodeId(), "temperature");
    Serial.printf("BatterySim: ready on node '%s'\n", getNodeId());
    return true;
}

void BatterySim::loop() {
    if (millis() - _last_update < 5000) return;   // update every 5s
    _last_update = millis();

    // Random-walk the SoC between 20% and 95%, reversing at each rail.
    _soc_pct += _dir;
    if (_soc_pct <= 20) _dir = 1;
    if (_soc_pct >= 95) _dir = -1;

    float current = (_dir < 0) ? -5.0f : 8.0f;          // A (negative = discharging)
    float voltage = 51.2f + (_soc_pct - 50) * 0.04f;    // crude SoC->voltage curve
    float power   = voltage * current;                  // W

    _soc.set(_soc_pct);          _soc.publish();
    _voltage.set(voltage);       _voltage.publish();
    _current.set(current);       _current.publish();
    _power.set(power);           _power.publish();
    _temperature.set(25.0f);     _temperature.publish();
}
