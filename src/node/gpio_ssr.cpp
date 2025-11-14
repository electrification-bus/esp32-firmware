#include <Arduino.h>
#include <node/gpio_ssr.h>
#include <pins.h>
#include <config.h>

#define DEBUG_SSR false

bool LOW_TRIGGER = true;
int last_toggle = millis();

SSR::SSR(const char* homie_node_id) : NodeEntity(homie_node_id) {
    _state_property.setup_settable(getNodeId(), "state", &NodeEntity::entity_settable_callback,(NodeEntity*)this);
    pinMode(SSR1_PIN, OUTPUT);
    setup();
}

void SSR::set_on_state() {
    digitalWrite(SSR1_PIN, LOW_TRIGGER ? LOW : HIGH);
    _state = true;
}

void SSR::set_off_state() {
    digitalWrite(SSR1_PIN, LOW_TRIGGER ? HIGH : LOW);
    _state = false;
}

void SSR::set_ssr_state(bool onoff) {
    if (onoff) {
        set_on_state();
    } else {
        set_off_state();
    }
    Serial.printf("SSR STATE: %s\n", _state ? "ON" : "OFF");
    _state_property.set_value_and_publish(_state);
}

void SSR::loop() {
    //if we're debugging, toggle the SSR every 3 seconds
    if (DEBUG_SSR && (millis() - last_toggle > 3000)) {
        Serial.printf("SSR: toggling state from %s to %s\n", _state ? "ON" : "OFF", _state ? "OFF" : "ON");
        last_toggle = millis();
        set_ssr_state(!_state);
    }
}

void SSR::set(bool onOff) {
    Serial.printf("SSR: external SET: 'state' to %s\n", onOff ? "ON" : "OFF");
    set_ssr_state(onOff);
}

void SSR::entity_settable_callback(Property* property) {
    //Serial.printf("SSR: entity_settable_callback called for property '%s' with value '%s'\n", property->id(), property->value());
    Serial.printf("SSR: entity_settable_callback called ON SSR\n");
    if (strcmp(property->id(), "state") == 0) {
        if (strcmp(property->value(), "true") == 0 || strcmp(property->value(), "1") == 0 || strcmp(property->value(), "on") == 0 || strcmp(property->value(), "yes") == 0) {
            set_ssr_state(true);
        } else if (strcmp(property->value(), "false") == 0 || strcmp(property->value(), "0") == 0 || strcmp(property->value(), "off") == 0 || strcmp(property->value(), "no") == 0) {
            set_ssr_state(false);
        } else {
            Serial.printf("SSR: invalid boolean value '%s' for property 'state'\n", property->value());
        }
    }
}