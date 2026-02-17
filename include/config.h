#pragma once
#include <Arduino.h>
#include <ArduinoYaml.h>
#include <pins.h>
#include <util/jsonUtils.h>
#include <homie/homie.h>
#include <homie/Device.h>


#define MAX_DEVICE_ID_CHARS   32 //TODO all string sizes

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

// TLS/SSL support for MQTT (port 8883)
#define MQTT_SECURE_PORT 8883
#define MQTT_CA_CERT_PATH "/certs/shiftr.io.ca"

// Returns true if MQTT should use TLS (port 8883)
bool mqtt_is_secure();

// Load CA certificate from SPIFFS for TLS connection
// Returns pointer to certificate data (static buffer) or nullptr on failure
const char* mqtt_load_ca_cert();

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