#pragma once

#include <Arduino.h>

// Set true in any node constructor that requires Bluetooth.
// Read in main setup() to decide whether to start WiFi STA.
extern bool NODE_REQUIRES_BLUETOOTH;

void setup_ebus_mdns();
void setup_homie();
void loop_homie();