#pragma once
#include <Arduino.h>

// Persist captive portal settings (WiFi, MQTT) to NVS via Arduino Preferences.
// Portal-saved values override config.yml — the user explicitly configured them.

// Save WiFi credentials to NVS (called from captive portal on user submit)
void portal_prefs_save_wifi(const char* ssid, const char* password);

// Save MQTT broker settings to NVS (called from captive portal on user submit)
void portal_prefs_save_mqtt(const char* host, uint16_t port, const char* user, const char* pass);

// Save device passphrase to NVS (called from captive portal on user submit)
void portal_prefs_save_passphrase(const char* passphrase);

// Load saved values into globals, overriding config.yml where portal values exist.
// Call after loadPlatformConfiguration().
void portal_prefs_load_fallbacks();

// Clear all portal-saved settings (WiFi + MQTT + passphrase) so next boot uses config.yml only.
void portal_prefs_clear();

// Auth rate limiting — returns true if locked out (too many failures)
#define AUTH_MAX_FAILURES 5
#define AUTH_LOCKOUT_MS   (5UL * 60UL * 1000UL)  // 5 minutes

bool portal_auth_is_locked();
void portal_auth_record_failure();
void portal_auth_reset();
