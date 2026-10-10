#pragma once

#include <Arduino.h>

class Device;

void setup_ebus_mdns();
void setup_homie();
void loop_homie();
// Controller builds only (-DENABLE_CONTROLLER, set from device.yml's homie_role by
// `ebus-esp32 generate`): the top-level links: list. The generator always emits both
// pairs — empty when the role does not use them — so either main.cpp path links.
void setup_controller_links();
void loop_controller_links();
void register_homie_properties(Device* device);
// Entries in the settable subscriber table (src/platform/mqtt_client.cpp). Generated
// from device.yml; optional in a hand-coded src/homie.cpp (the default is 32).
uint16_t settable_table_capacity();