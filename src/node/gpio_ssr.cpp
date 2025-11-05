#include <Arduino.h>
#include <node/gpio_ssr.h>
#include <node/EntityManager.h>
#include <pins.h>

#define DEBUG_SSR false

bool LOW_TRIGGER = true;
int last_toggle = millis();

SSR::SSR() : NodeEntity() {
    pinMode(SSR1_PIN, OUTPUT);
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
    entityManager->setNodeProperty(_homie_node_id, "state", _state);
    entityManager->publish(_homie_node_id);
}

void SSR::loop() {
    //if we're debugging, toggle the SSR every 3 seconds
    if (DEBUG_SSR && (millis() - last_toggle > 3000)) {
        Serial.printf("SSR: toggling state from %s to %s\n", _state ? "ON" : "OFF", _state ? "OFF" : "ON");
        last_toggle = millis();
        set_ssr_state(!_state);
    }
}
