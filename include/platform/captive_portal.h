#pragma once
#include <Arduino.h>

// Start configuration web server.
// With CONFIG_WIFI_AP: WiFi AP + DNS redirect + captive portal + all API endpoints
// Without CONFIG_WIFI_AP: HTTP server on port 80 (ethernet) with API endpoints only
void captive_portal_start();

// Loop handler - must be called from main loop.
// With CONFIG_WIFI_AP: processes DNS requests.
// Without CONFIG_WIFI_AP: no-op.
void captive_portal_loop();

// Returns true (once) when MQTT configuration has been saved via the web server.
// Caller should restart MQTT connection with new settings.
bool captive_portal_mqtt_changed();
