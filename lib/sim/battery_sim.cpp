#include <Arduino.h>
#include <sim/battery_sim.h>
#include <homie/Device.h>     // calls device()->…

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

    // G1: simulate the proxier's link to the source — "up" for 30s, "down" for 10s,
    // repeating. On a transition, reflect link health onto the proxied child's Homie
    // state (READY/LOST) via the proxy state-management primitive. While down, the
    // source is unreachable, so publish no new values (the last-known stay retained,
    // and $state=lost tells consumers they're stale).
    bool link_up = (millis() / 1000) % 40 < 30;
    if (link_up != _link_up) {
        _link_up = link_up;
        device()->setSourceConnected(link_up);   // proxy state management (proxy.md)
    }
    if (!link_up) return;   // source link down — don't publish stale readings

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
