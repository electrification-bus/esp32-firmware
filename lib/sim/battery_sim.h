#pragma once
#include <node/NodeEntity.h>

// Simulated battery (BESS) source for the nested bridge reference example (D5).
//
// Publishes plausible state-of-charge + electrical values with NO external hardware,
// so the "ESP32-as-bridge -> battery-as-child" Homie 5 nesting pattern is fully
// bench-testable. To bridge a REAL JBD BMS instead, swap this component for
// `ble/serial_bms` in the child's `nodes:` — the child-device structure is identical;
// only the data source changes. See doc/NestedBridgeExample.md.
class BatterySim : public NodeEntity {
public:
    BatterySim(const char* homie_node_id) : NodeEntity(homie_node_id) {}
    bool setup() override;
    void loop() override;
private:
    NodeProperty _soc, _voltage, _current, _power, _temperature;
    unsigned long _last_update = 0;
    int  _soc_pct = 72;
    int  _dir     = -1;   // -1 discharging, +1 charging; flips at the SoC rails
};
