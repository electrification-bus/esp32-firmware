#pragma once
#include <Arduino.h>

// Start WiFi AP + DNS + web server for captive portal
// Portal runs continuously in the background for (re)configuration
// WiFi is set to AP+STA mode: AP serves the portal, STA connects to a network
void captive_portal_start();

// Loop handler - must be called from main loop for DNS processing
// AsyncWebServer handles HTTP requests automatically
void captive_portal_loop();

// Returns true (once) when MQTT configuration has been saved via the portal
// Caller should restart MQTT connection with new settings
bool captive_portal_mqtt_changed();
