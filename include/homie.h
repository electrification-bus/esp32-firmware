#pragma once

#include <Arduino.h>

class Device;

void setup_ebus_mdns();
void setup_homie();
void loop_homie();
void register_homie_properties(Device* device);
// Entries in the settable subscriber table (src/platform/mqtt_client.cpp). Generated
// from device.yml; optional in a hand-coded src/homie.cpp (the default is 32).
uint16_t settable_table_capacity();