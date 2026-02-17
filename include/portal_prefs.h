#pragma once
#include <Arduino.h>

// Persist captive portal settings (WiFi, MQTT) to NVS via Arduino Preferences.
// Portal-saved values override config.yml — the user explicitly configured them.

// Save WiFi credentials to NVS (called from captive portal on user submit)
void portal_prefs_save_wifi(const char* ssid, const char* password);

// Save MQTT broker settings to NVS (called from captive portal on user submit)
void portal_prefs_save_mqtt(const char* host, uint16_t port, const char* user, const char* pass);

// Load saved values into globals, overriding config.yml where portal values exist.
// Call after loadPlatformConfiguration().
void portal_prefs_load_fallbacks();
