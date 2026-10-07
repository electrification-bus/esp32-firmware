#pragma once
#include <Arduino.h>

// Persist captive portal settings (WiFi, MQTT, OTA URLs) to NVS via Arduino
// Preferences. Portal-saved values override config.yml — the user explicitly
// configured them.

// Save WiFi credentials to NVS (called from captive portal on user submit)
void portal_prefs_save_wifi(const char* ssid, const char* password);

// Save MQTT broker settings to NVS (called from captive portal on user submit)
void portal_prefs_save_mqtt(const char* host, uint16_t port, const char* user, const char* pass);

// Save device passphrase to NVS (called from captive portal on user submit)
void portal_prefs_save_passphrase(const char* passphrase);

// Save OTA firmware / filesystem image URLs to NVS (called from captive portal
// when an OTA update is started with a URL)
void portal_prefs_save_firmware_url(const char* url);
void portal_prefs_save_fs_url(const char* url);

// True if the OTA URL was set via the portal (NVS or this session) rather than
// inherited from config.yml — used to decide whether the UI should prefill it.
bool portal_prefs_has_fw_url();
bool portal_prefs_has_fs_url();

// Load saved values into globals, overriding config.yml where portal values exist.
// Call after loadPlatformConfiguration().
void portal_prefs_load_fallbacks();

// Recovery copy of the device passphrase: the last one a successfully loaded config
// carried. Read only when no config can be loaded, so the admin endpoints (and with
// them POST /api/v1/ota/filesystem) still accept the passphrase the filesystem had.
// Save writes NVS only when the value changed. Load returns false if none is stored.
void portal_prefs_save_recovery_passphrase(const char* passphrase);
bool portal_prefs_load_recovery_passphrase();

// Clear all portal-saved settings (WiFi + MQTT + OTA URLs + passphrase) so next boot uses config.yml only.
void portal_prefs_clear();

// Auth rate limiting — returns true if locked out (too many failures)
#define AUTH_MAX_FAILURES 5
#define AUTH_LOCKOUT_MS   (5UL * 60UL * 1000UL)  // 5 minutes

bool portal_auth_is_locked();
void portal_auth_record_failure();
void portal_auth_reset();

// --- Portal session tokens (RAM only) ---
// POST /api/v1/login (passphrase as Basic auth) and POST /api/v1/auth/register
// (passphrase in a JSON body) exchange the device passphrase for a random session
// token; admin endpoints then accept it in X-Session-Token or as a bearer token
// instead of the passphrase. One slot: a new token replaces the previous one.
// Invalidated by idle/absolute timeout, passphrase change, reset-prefs, or reboot.
#define SESSION_IDLE_TIMEOUT_MS (15UL * 60UL * 1000UL)       // 15 min inactivity
#define SESSION_MAX_LIFETIME_MS (2UL * 60UL * 60UL * 1000UL) // 2 h absolute cap

const char* portal_session_create();           // issue new token (32 hex chars)
bool portal_session_valid(const char* token);  // constant-time; refreshes idle timer
void portal_session_clear();                   // invalidate the active session
