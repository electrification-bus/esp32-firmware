#pragma once
#include <node/NodeEntity.h>

// A single GPIO driven as a digital output, exposed as a settable boolean property.
//
// Deliberately generic: this is `pinMode(OUTPUT)` + `digitalWrite()`, which is identical
// whether the pin drives a solid-state relay, a mechanical relay, a contactor coil, a
// MOSFET gate, an LED, a valve or a buzzer. What differs between those is polarity, pin
// choice and wiring — configuration and hardware, not code — so there is one component
// rather than one per device. See examples/ssr/ for the relay-specific wiring knowledge.
//
// `state` is settable, so a controller can drive it by publishing to <node>/state/set, and
// another component can drive it locally via deliver_local_set() — no broker round-trip, so
// it keeps working with MQTT down.

class DigitalOutput : public NodeEntity {
public:
    // active_low     true when the load is energised by pulling the pin LOW, which is
    //                what most opto-isolated relay and SSR boards expect.
    // initial_state  logical state applied in setup(). Defaults to false at every call
    //                site; energising a load at boot should always be a deliberate choice.
    DigitalOutput(const char* homie_node_id, int pin, bool active_low, bool initial_state)
        : NodeEntity(homie_node_id), _pin(pin), _active_low(active_low),
          _initial_state(initial_state) {}
    ~DigitalOutput() {}

    bool setup() override;
    void loop() override;

    void set(bool on) override;
    bool getState() const { return _state; }

    // Both overloads on purpose: which one the MQTT dispatch calls depends on whether
    // Property::subscribe() had already registered the /set topic when NodeProperty::setup()
    // ran, and that depends on whether the broker was connected at boot. Implementing only
    // the Property* form leaves the output unresponsive on a device that came up offline.
    bool entity_settable_callback(Property* property) override;
    bool entity_settable_callback(const char* property_id, const char* value) override;

private:
    void applyState(bool on);

    NodeProperty _state_property;

    int  _pin;
    bool _active_low;
    bool _initial_state;
    bool _state = false;
};
