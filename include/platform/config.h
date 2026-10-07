#pragma once
#include <Arduino.h>
#include <util/jsonUtils.h>
#include <homie/homie.h>
#include <homie/Device.h>
#include <platform/broker_discovery.h>


#define MAX_DEVICE_ID_CHARS   64 // <device_name>_<6 hex MAC chars>

// POSIX TZ string for local time — override in platformio.ini build_flags if needed
// Examples: "EST5EDT,M3.2.0,M11.1.0"  "PST8PDT,M3.2.0,M11.1.0"  "UTC0"
#ifndef NTP_TIMEZONE
#define NTP_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"
#endif

extern char wifi_ssid[33];
extern char wifi_password[64];
// WiFi STA on/off (config.yml wifi.enabled): default true on CONFIG_WIFI_AP builds, false
// on Ethernet builds. Applies only once a configuration has loaded; recovery mode joins
// NVS-saved WiFi regardless (see setup_wifi_sta()).
extern bool wifi_sta_enabled;
// True once loadConfiguration() succeeded; false in recovery mode.
extern bool config_loaded;

extern char mqtt_host[64];
extern uint16_t mqtt_port;
extern char mqtt_username[64];
extern char mqtt_password[64];
extern char device_id_prefix[32];

// Device info for mDNS advertisement
extern char device_name[32];
extern char device_type[HOMIE_TYPE_MAXLEN];   // eBus device types up to 41 chars
extern char device_manufacturer[32];
extern char device_model[32];
extern char device_serial[32];
extern char firmware_name[32];
extern char firmware_version[16];
extern char firmware_url[128];
extern char fs_url[128];

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
  MQTT_MODE_DISCOVERY_ONLY = 0,      // mDNS only, ignore config
  MQTT_MODE_DISCOVERY_WITH_FALLBACK, // mDNS first, fall back to config
  MQTT_MODE_CONFIGURED_ONLY          // config only, no mDNS discovery (default)
};
// NOTE: a discovery mode (not CONFIGURED_ONLY) auto-enables mdns_discovery at config
// load, so the mode and the querier gate can't be set incoherently.

extern MqttMode mqtt_mode;

// Broker service types the mDNS querier browses, in priority order (config.yml
// mqtt.discover; default secure-mqtt, then mqtt).
extern BrokerDiscoveryList mqtt_discover;

// mDNS master switch (config.yml mdns.enabled, default true). false means no mDNS at all:
// no MDNS.begin(), no <device id>.local claim, no querier, no service records. A discovery
// MqttMode turns it back on for the querier at config load (see config.cpp).
extern bool mdns_enabled;

// With mdns_enabled on, mDNS is split into two independent gates; their default depends
// on the build (see config.cpp):
//   mdns_discovery — querier: browse for the MQTT broker (_mqtt._tcp / _secure-mqtt._tcp)
//                    and resolve .local names. The crash-safe client side; this is the
//                    high-value path (find the broker) and is what discovery MqttModes need.
//   mdns_advertise — responder: register the _device-info / _http / _telnet / _ebus service
//                    records (TXT-heavy) so other hosts can find this device. This is where
//                    the bundled ESP-IDF responder in arduino-esp32 2.0.x can crash on
//                    certain inbound packets.
// On arduino-esp32 3.x the <device id>.local hostname is claimed whenever mdns_enabled is
// on, so `mdns: {advertise: false}` there keeps the hostname and drops the service records.
// NOTE (Level-1 limitation): the Arduino MDNS API couples mdns_init() with a hostname
// claim, so mdns_discovery still brings up a *minimal* hostname responder; only the
// heavy service/TXT advertising is gated by mdns_advertise.
extern bool mdns_discovery;
extern bool mdns_advertise;

// Serial-over-TCP console port (see include/platform/log_stream.h). The device's
// Serial.printf output is streamed to any TCP client here, in addition to the USB
// UART, so an installed board can be monitored with:
//     pio device monitor --port socket://<device-ip>:<port>
// Config key `log_tcp_port`; 0 disables the listener. NOTE: the stream is
// unauthenticated — anything the firmware prints is readable by anyone on the LAN.
extern uint16_t log_tcp_port;

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

// Read the ESP32's factory eFuse MAC (the chip's unique, singular hardware id) into
// out[6], in standard MAC byte order. Prefer this over a per-interface MAC: boards
// like the Olimex POE-ISO expose separate WiFi and Ethernet MACs, but there is only
// one chip id. Globally unique (48-bit) and stable across reboots/reflashes.
void chip_unique_id(uint8_t out[6]);
void generateDeviceID();
const char* getDeviceID();
bool loadConfiguration();
bool loadPlatformConfiguration();
bool loadHomieConfiguration();
JsonObject* get_config();
JsonDocument* get_node_config();
JsonDocument* get_platform_config();
JsonDocument* get_homie_config();

// theDevice and g_active_setup_device now live in homie/Device.h, next to the class they
// are instances of. config.h includes it, so anything that got them from here still does
// — but NodeEntity.h and NodeProperty can now reach them without pulling the rest of
// config.h into every driver translation unit.