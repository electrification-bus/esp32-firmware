#pragma once

#include <Arduino.h>

class Device;

void setup_ebus_mdns();
void setup_homie();
void loop_homie();
void register_homie_properties(Device* device);