#include <Arduino.h>
#include <gpio/digital_output.h>
#include <platform/config.h>

// Accepts the spellings a human or a controller might publish.
static bool truthy(const char* v) {
    return strcmp(v, "true") == 0 || strcmp(v, "1") == 0 ||
           strcmp(v, "on")   == 0 || strcmp(v, "yes") == 0;
}
static bool falsy(const char* v) {
    return strcmp(v, "false") == 0 || strcmp(v, "0") == 0 ||
           strcmp(v, "off")   == 0 || strcmp(v, "no") == 0;
}

bool DigitalOutput::setup() {
    NodeEntity::setup();
    _state_property.setup(getNodeId(), "state",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);

    // Drive the pin to the requested level BEFORE switching it to an output, so a
    // hardware pull-up on an active-low board cannot glitch the load on for the
    // microseconds between pinMode() and the first digitalWrite().
    digitalWrite(_pin, (_initial_state != _active_low) ? HIGH : LOW);
    pinMode(_pin, OUTPUT);
    applyState(_initial_state);

    Serial.printf("DOUT[%s]: pin=%d active_%s initial=%s\n", getNodeId(), _pin,
                  _active_low ? "low" : "high", _initial_state ? "ON" : "OFF");
    _state_property.set_value_and_publish(_state);
    return true;
}

void DigitalOutput::loop() {
    // Nothing to poll — the output is driven entirely by /set, whether that arrives from a
    // controller over MQTT or locally from another component via deliver_local_set().
}

void DigitalOutput::applyState(bool on) {
    // active_low inverts the electrical level, never the logical one: `state` always
    // means "the load is energised", whatever the board's polarity.
    digitalWrite(_pin, (on != _active_low) ? HIGH : LOW);
    _state = on;
}

void DigitalOutput::set(bool on) {
    if (on == _state) return;
    applyState(on);
    Serial.printf("DOUT[%s]: %s\n", getNodeId(), _state ? "ON" : "OFF");
    _state_property.set_value_and_publish(_state);
}

void DigitalOutput::entity_settable_callback(Property* property) {
    if (!property) return;
    entity_settable_callback(property->id(), property->value());
}

void DigitalOutput::entity_settable_callback(const char* property_id, const char* value) {
    if (strcmp(property_id, "state") != 0) {
        Serial.printf("DOUT[%s]: unhandled settable '%s'\n", getNodeId(), property_id);
        return;
    }
    if      (truthy(value)) set(true);
    else if (falsy(value))  set(false);
    else Serial.printf("DOUT[%s]: invalid boolean '%s' for 'state'\n", getNodeId(), value);
}
