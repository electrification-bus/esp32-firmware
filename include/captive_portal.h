#pragma once
#include <Arduino.h>

// Returns true if captive portal should be activated (no broker configured)
bool captive_portal_needed();

// Start WiFi AP + DNS + web server for captive portal
// Blocks until MQTT config is saved, then returns for normal startup
void captive_portal_start();

// Loop handler for DNS processing (AsyncWebServer handles itself)
void captive_portal_loop();
