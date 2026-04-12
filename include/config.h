#pragma once
#include <Arduino.h>
#include <ArduinoYaml.h>
#include <pins.h>
#include <util/jsonUtils.h>
#include <homie/homie.h>
#include <homie/Device.h>


#define MAX_DEVICE_ID_CHARS   64 // <device_name>_<6 hex MAC chars>

// POSIX TZ string for local time — override in platformio.ini build_flags if needed
// Examples: "EST5EDT,M3.2.0,M11.1.0"  "PST8PDT,M3.2.0,M11.1.0"  "UTC0"
#ifndef NTP_TIMEZONE
#define NTP_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"
#endif

extern char wifi_ssid[33];
extern char wifi_password[64];

extern char mqtt_host[64];
extern uint16_t mqtt_port;
extern char mqtt_username[64];
extern char mqtt_password[64];
extern char device_id_prefix[32];

// Device info for mDNS advertisement
extern char device_name[32];
extern char device_type[32];
extern char device_manufacturer[32];
extern char device_model[32];
extern char device_serial[32];
extern char firmware_name[32];
extern char firmware_version[16];
extern char firmware_url[128];

// Homie role: "device" or "controller"
extern char homie_role[16];

// Device passphrase for authenticated REST endpoints (OTA, registration)
extern char device_passphrase[64];

// OTA enable/disable via REST API
// OTA_ENABLED_TIMEOUT: minutes before OTA auto-disables (0 = no timeout)
#ifndef OTA_ENABLED_TIMEOUT
#define OTA_ENABLED_TIMEOUT 10
#endif

bool ota_is_enabled();
void ota_set_enabled(bool enabled);
void ota_check_timeout();

// MQTT broker resolution mode
enum MqttMode {
  MQTT_MODE_DISCOVERY_ONLY = 0,      // mDNS only, ignore config (default)
  MQTT_MODE_DISCOVERY_WITH_FALLBACK, // mDNS first, fall back to config
  MQTT_MODE_CONFIGURED_ONLY          // config only, no mDNS discovery
};

extern MqttMode mqtt_mode;

// TLS/SSL support for MQTT (port 8883)
#define MQTT_SECURE_PORT 8883

// Returns true if MQTT should use TLS (port 8883)
bool mqtt_is_secure();

// Build the SPIFFS cert path for a hostname: e.g. "mqtt.example.com" -> "/certs/mqttExampleCom.ca"
void mqtt_build_cert_path(const char* hostname, char* out, size_t out_len);

// Load CA certificate from SPIFFS for TLS connection
// Returns pointer to certificate data (static buffer) or nullptr on failure
const char* mqtt_load_ca_cert();

// Invalidate cached CA cert (call when broker changes so next connect re-loads)
void mqtt_invalidate_ca_cert();

// Returns true if mqtt_host is configured (non-empty)
bool has_mqtt_broker();

void generateDeviceID();
const char* getDeviceID();
bool loadConfiguration();
bool loadPlatformConfiguration();
bool loadHomieConfiguration();
JsonObject* get_config();
JsonDocument* get_node_config();
JsonDocument* get_platform_config();
JsonDocument* get_homie_config();

extern Device theDevice;