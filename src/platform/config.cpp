#include <platform/config.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoYaml.h>
#include <util/jsonUtils.h>
#include <platform/portal_prefs.h>
#include <platform/network.h>
#include <homie/homie_id.h>

Device theDevice;
Device* g_active_setup_device = nullptr;  // see config.h; set by NodeEntity::setup()

static JsonObject config_json;
static JsonDocument homie_config_json;
static JsonDocument platform_config_json;
static JsonDocument node_config_json;
char wifi_ssid[33] = {0};
char wifi_password[64] = {0};

char mqtt_host[64] = {0};
uint16_t mqtt_port;
uint8_t mqtt_qos = 2;   // QoS for retained Homie publications; Homie 5 recommends 2 (C4)
bool mdns_discovery = false;   // mDNS querier (broker discovery) — off by default (ESP32-6du.6)
bool mdns_advertise = false;   // mDNS responder (service advertising) — off by default; ESP32-9mx crash surface
char mqtt_username[64] = {0};
char mqtt_password[64] = {0};
char device_id_prefix[32] = {0};

// Device info for mDNS advertisement
char device_name[32] = {0};
char device_type[HOMIE_TYPE_MAXLEN] = {0};   // eBus device types up to 41 chars (rrj.5)
char device_manufacturer[32] = "NESL";
char device_model[32] = "ESP32";
char device_serial[32] = {0};
char firmware_name[32] = {0};
char firmware_version[16] = "1.0.0";
char firmware_url[128] = {0};
char fs_url[128] = {0};

// Homie role: "device" or "controller"
char homie_role[16] = "device";

// Device passphrase for authenticated REST endpoints
char device_passphrase[64] = {0};

// OTA enable state and timeout tracking
static bool _ota_enabled = false;
static unsigned long _ota_enabled_at = 0;

// Default; overridden by mqtt_mode key in config.yml.
MqttMode mqtt_mode = MQTT_MODE_DISCOVERY_ONLY;

const char* UNIQUE_ID = "";
char device_id[MAX_DEVICE_ID_CHARS] = {0};

const char* platform_config_file = "/config.yml";
const char* homie_config_file = "/homie.yml";
const char* homie_config_file_backup = "/homie_config_from_SD.yml";
const char* config_file_backup = "/config_from_SD.yml";

// TLS certificate buffer - sized for typical CA certs (up to 4KB)
#define CA_CERT_MAX_SIZE 4096
static char ca_cert_buffer[CA_CERT_MAX_SIZE] = {0};
static bool ca_cert_loaded = false;

const bool default_value = false; // default value for property
bool current_value = default_value;
bool config_loaded = false; // prevent updates if config isn't loaded

JsonObject* get_config() {
    return &config_json;
}
JsonDocument* get_node_config() {
    return &node_config_json;
}
JsonDocument* get_homie_config() {
    return &homie_config_json;
}
JsonDocument* get_platform_config() {
    return &platform_config_json;
}
// Device-id template from device.yml `id:` (config.yml `device_id`), e.g. "{chip_id}"
// or "bess-{chip_id}". Resolved at boot in generateDeviceID(). Empty -> "{chip_id}".
static char device_id_template[96] = {0};

void chip_unique_id(uint8_t out[6]) {
  // ESP.getEfuseMac() packs the 6-byte factory MAC into the low bytes of a uint64
  // (little-endian), so byte i of the standard MAC order is (mac >> 8*i) & 0xFF.
  uint64_t mac = ESP.getEfuseMac();
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)(mac >> (8 * i));
}

void generateDeviceID() {
  // Resolve the device.yml `id:` template against the ESP32's factory eFuse MAC (the
  // chip's singular hardware id — not a per-interface MAC; this Olimex POE board has
  // both a WiFi and an Ethernet MAC). Placeholders: {chip_id} (full), {chip_id_short}
  // (last 3 bytes), {name}. Empty template -> "{chip_id}". The result is Homie-legal.
  uint8_t chip_id[6];
  chip_unique_id(chip_id);
  resolve_device_id(device_id_template, chip_id, device_name, device_id, sizeof(device_id));
  // A device-id is an OPAQUE topic-level identifier — consumers use $type + info for
  // identity, never parse the id. If the template carries no per-unit uniqueness source,
  // warn: two identical units would collide on the broker.
  if (!strstr(device_id_template, "{chip_id")) {
    Serial.printf("CONFIG: WARNING device id '%s' is fixed (no {chip_id}/{chip_id_short} "
                  "in the template) — multiple units will collide on the broker\n", device_id);
  }
  Serial.printf("Device ID: %s\n", device_id);
}

const char* getDeviceID() {
  return device_id;
}

bool loadConfiguration() {
  Serial.println("CONFIG: mounting LittleFS");
  if (!LittleFS.begin(false)) {
    Serial.println("CONFIG: LittleFS mount failed — filesystem may need formatting or re-uploading");
    return false;
  }

  // List saved certificates
  Serial.println("CERTS: list certificates in /certs folder");
  File certsDir = LittleFS.open("/certs");
  if (certsDir && certsDir.isDirectory()) {
    Serial.println("CERTS: /certs/");
    File f = certsDir.openNextFile();
    while (f) {
      Serial.printf("CERTS:   %s (%d bytes)\n", f.name(), f.size());
      f = certsDir.openNextFile();
    }
  } else {
    Serial.println("CERTS: /certs/ not found (no saved certificates)");
  }

  if (!loadPlatformConfiguration()) {
    Serial.println("PLATFORM CONFIG: **ERROR -- failed to load platform configuration");
    return false;
  }
  // Fill any gaps left by config.yml with previously saved portal preferences (NVS)
  portal_prefs_load_fallbacks();

  if (!loadHomieConfiguration()) {
    Serial.println("HOMIE CONFIG: **ERROR -- failed to load homie configuration");
    return false;
  }
  config_loaded = true;

  // Emit MAC addresses for debugging/identification
  Serial.printf("CONFIG: Ethernet MAC: %s\n", get_eth_mac());
  Serial.printf("CONFIG: WiFi MAC: %s\n", get_wlan_mac());

  return true;
}

bool loadPlatformConfiguration() {
  File file;

  //see if we have a config from the build (SPIFFS)
  if (LittleFS.exists(platform_config_file)) {
    file = LittleFS.open(platform_config_file, FILE_READ);
    Serial.println("CONFIG: LOAD from SPIFFS default");
  } else if (LittleFS.exists(config_file_backup)) {
    file = LittleFS.open(config_file_backup, FILE_READ);
    Serial.println("CONFIG: LOAD from SPIFFS backup (config_from_SD.json)");
  }
  if (!file) {
    Serial.println("CONFIG: NO SPIFFS CONFIG (overriding or backup)");
  
    return false;
  }
  JsonDocument yml_config; // https://arduinojson.org/v6/assistant to compute the capacity of static (deprecated).
  Serial.printf("CONFIG: YAML config:'%s'\n", platform_config_file);
  DeserializationError error = deserializeYml( yml_config, file ); // convert yaml to json

  // Deserialize the JSON document
  file.close();
  if (error) {
    Serial.printf("***** CONFIG: FAILED to read config: code:'%d'\n", error.code());
    //SD.end();
  
    return false;
  }
  Serial.println(F("CONFIG: DESERIALIZE OK"));
  serializeJsonPretty(yml_config, Serial);
  Serial.println();

  //TODO keys to #define header
  JsonArray conf_array = yml_config["config"].as<JsonArray>();
  config_json = conf_array[0].as<JsonObject>();

  // Safely copy string values with null checks
  if (config_json["wifi_ssid"].is<JsonString>()) {
    strncpy(wifi_ssid, config_json["wifi_ssid"].as<JsonString>().c_str(), sizeof(wifi_ssid) - 1);
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
  }

  if (config_json["wifi_password"].is<JsonString>()) {
    strncpy(wifi_password, config_json["wifi_password"].as<JsonString>().c_str(), sizeof(wifi_password) - 1);
    wifi_password[sizeof(wifi_password) - 1] = '\0';
  }

  if (config_json["mqtt_host"].is<JsonString>()) {
    strncpy(mqtt_host, config_json["mqtt_host"].as<JsonString>().c_str(), sizeof(mqtt_host) - 1);
    mqtt_host[sizeof(mqtt_host) - 1] = '\0';
  }

  if (config_json["mqtt_port"].is<uint16_t>()) {
    mqtt_port = config_json["mqtt_port"].as<uint16_t>();
  }

  if (config_json["mqtt_qos"].is<int>()) {
    int q = config_json["mqtt_qos"].as<int>();
    if (q >= 0 && q <= 2) mqtt_qos = (uint8_t)q;   // QoS for retained Homie publications (C4)
    else Serial.printf("CONFIG: ignoring invalid mqtt_qos=%d (must be 0-2)\n", q);
  }

  // mDNS gates (ESP32-6du.6). Legacy `mdns_enabled` sets BOTH; granular keys override.
  if (config_json["mdns_enabled"].is<bool>()) {
    bool en = config_json["mdns_enabled"].as<bool>();
    mdns_discovery = en;
    mdns_advertise = en;
  }
  if (config_json["mdns_discovery"].is<bool>()) {
    mdns_discovery = config_json["mdns_discovery"].as<bool>();
  }
  if (config_json["mdns_advertise"].is<bool>()) {
    mdns_advertise = config_json["mdns_advertise"].as<bool>();
  }

  if (config_json["mqtt_username"].is<JsonString>()) {
    strncpy(mqtt_username, config_json["mqtt_username"].as<JsonString>().c_str(), sizeof(mqtt_username) - 1);
    mqtt_username[sizeof(mqtt_username) - 1] = '\0';
  }

  if (config_json["mqtt_password"].is<JsonString>()) {
    strncpy(mqtt_password, config_json["mqtt_password"].as<JsonString>().c_str(), sizeof(mqtt_password) - 1);
    mqtt_password[sizeof(mqtt_password) - 1] = '\0';
  }

  if (config_json["device_id_prefix"].is<JsonString>()) {
    strncpy(device_id_prefix, config_json["device_id_prefix"].as<JsonString>().c_str(), sizeof(device_id_prefix) - 1);
    device_id_prefix[sizeof(device_id_prefix) - 1] = '\0';
  }

  // device_id is a TEMPLATE (e.g. "{chip_id}", "bess-{chip_id}"); resolved at boot in
  // generateDeviceID(). Empty -> "{chip_id}".
  if (config_json["device_id"].is<JsonString>()) {
    strncpy(device_id_template, config_json["device_id"].as<JsonString>().c_str(), sizeof(device_id_template) - 1);
    device_id_template[sizeof(device_id_template) - 1] = '\0';
  }

  // device_name is loaded from homie.yml (name field), not config.yml

  if (config_json["device_type"].is<JsonString>()) {
    strncpy(device_type, config_json["device_type"].as<JsonString>().c_str(), sizeof(device_type) - 1);
    device_type[sizeof(device_type) - 1] = '\0';
  }

  if (config_json["manufacturer"].is<JsonString>()) {
    strncpy(device_manufacturer, config_json["manufacturer"].as<JsonString>().c_str(), sizeof(device_manufacturer) - 1);
    device_manufacturer[sizeof(device_manufacturer) - 1] = '\0';
  }

  if (config_json["model"].is<JsonString>()) {
    strncpy(device_model, config_json["model"].as<JsonString>().c_str(), sizeof(device_model) - 1);
    device_model[sizeof(device_model) - 1] = '\0';
  }

  if (config_json["serial_number"].is<JsonString>()) {
    strncpy(device_serial, config_json["serial_number"].as<JsonString>().c_str(), sizeof(device_serial) - 1);
    device_serial[sizeof(device_serial) - 1] = '\0';
  }

  if (config_json["firmware_name"].is<JsonString>()) {
    strncpy(firmware_name, config_json["firmware_name"].as<JsonString>().c_str(), sizeof(firmware_name) - 1);
    firmware_name[sizeof(firmware_name) - 1] = '\0';
  }

  if (config_json["firmware_version"].is<JsonString>()) {
    strncpy(firmware_version, config_json["firmware_version"].as<JsonString>().c_str(), sizeof(firmware_version) - 1);
    firmware_version[sizeof(firmware_version) - 1] = '\0';
  }

  if (config_json["firmware_url"].is<JsonString>()) {
    strncpy(firmware_url, config_json["firmware_url"].as<JsonString>().c_str(), sizeof(firmware_url) - 1);
    firmware_url[sizeof(firmware_url) - 1] = '\0';
  }

  if (config_json["homie_role"].is<JsonString>()) {
    strncpy(homie_role, config_json["homie_role"].as<JsonString>().c_str(), sizeof(homie_role) - 1);
    homie_role[sizeof(homie_role) - 1] = '\0';
  }

  if (config_json["device_passphrase"].is<JsonString>()) {
    strncpy(device_passphrase, config_json["device_passphrase"].as<JsonString>().c_str(), sizeof(device_passphrase) - 1);
    device_passphrase[sizeof(device_passphrase) - 1] = '\0';
  }

  if (config_json["mqtt_mode"].is<JsonString>()) {
    const char* mode_str = config_json["mqtt_mode"].as<JsonString>().c_str();
    if (strcmp(mode_str, "configured-only") == 0) {
      mqtt_mode = MQTT_MODE_CONFIGURED_ONLY;
    } else if (strcmp(mode_str, "discovery-with-fallback") == 0) {
      mqtt_mode = MQTT_MODE_DISCOVERY_WITH_FALLBACK;
    } else {
      mqtt_mode = MQTT_MODE_DISCOVERY_ONLY;
    }
    Serial.printf("CONFIG: mqtt_mode=%s\n", mode_str);
  }

  return true;
}

bool loadHomieConfiguration() {
  File file;
  //HOMIE specific config
  if (LittleFS.exists(homie_config_file)) {
    file = LittleFS.open(homie_config_file, FILE_READ);
    Serial.println("HOMIE CONFIG: LOAD from SPIFFS default");
  } else if (LittleFS.exists(homie_config_file_backup)) {
    file = LittleFS.open(homie_config_file_backup, FILE_READ);
    Serial.println("HOMIE CONFIG: LOAD from SPIFFS backup (homie_config_from_SD.json)");
  }
  if (!file) {
    Serial.println("HOMIE CONFIG: NO SPIFFS CONFIG (overriding or backup)");
  
    return false;
  }
  JsonDocument homie_yml_config; // https://arduinojson.org/v6/assistant to compute the capacity of static (deprecated).
  Serial.printf("HOMIE CONFIG: YAML config:'%s'\n", homie_config_file);
  DeserializationError error = deserializeYml( homie_yml_config, file ); // convert yaml to json

  // Deserialize the JSON document
  file.close();
  if (error) {
    Serial.printf("***** HOMIE CONFIG: FAILED to read config: code:'%d'\n", error.code());
    //SD.end();
  
    return false;
  }
  Serial.println(F("HOMIE CONFIG: DESERIALIZE OK"));
  //serializeJsonPretty(homie_yml_config, Serial);
  //Serial.println();

  JsonObject homie_conf_obf = homie_config_json.to<JsonObject>();
  JsonObject homie_root = homie_yml_config.as<JsonObject>();
  if (jsonExists(homie_root[HOMIE_HOMIE])) homie_conf_obf[HOMIE_HOMIE] = homie_root[HOMIE_HOMIE]; else {Serial.printf("CONFIG:**ERROR: MISSING '%s'\n",HOMIE_HOMIE);return false;}

  if (jsonExists(homie_root[HOMIE_NAME])) {
    homie_conf_obf[HOMIE_NAME] = homie_root[HOMIE_NAME];
    // Use homie name as device_name (homie.yml is authoritative for device name)
    strncpy(device_name, homie_root[HOMIE_NAME].as<const char*>(), sizeof(device_name) - 1);
    device_name[sizeof(device_name) - 1] = '\0';
  } else {Serial.printf("CONFIG:**ERROR: MISSING '%s'\n",HOMIE_NAME);return false;}
  if (jsonExists(homie_root[HOMIE_TYPE])) {
    homie_conf_obf[HOMIE_TYPE] = homie_root[HOMIE_TYPE];
    // Use homie type as device_type if not already set
    if (strlen(device_type) == 0) {
      strncpy(device_type, homie_root[HOMIE_TYPE].as<const char*>(), sizeof(device_type) - 1);
      device_type[sizeof(device_type) - 1] = '\0';
    }
  } else {Serial.printf("CONFIG:**ERROR: MISSING '%s'\n",HOMIE_TYPE);return false;}
  if (jsonExists(homie_root[HOMIE_IMPLEMENTATION])) homie_conf_obf[HOMIE_IMPLEMENTATION] = homie_root[HOMIE_IMPLEMENTATION].as<const char*>(); else {Serial.printf("CONFIG:**ERROR: MISSING '%s'\n",HOMIE_IMPLEMENTATION);return false;}
  Serial.printf("HOMIE CONFIG:  DEVICE version %s name %s type%s\n", homie_root[HOMIE_HOMIE].as<const char*>(), homie_root[HOMIE_NAME].as<const char*>(),homie_root[HOMIE_TYPE].as<const char*>());

  /*** process the nodes */
  int num_nodes = 0;
  for (JsonObject node : homie_yml_config[HOMIE_NODES].as<JsonArray>()) {
    if (jsonExists(node[HOMIE_NAME])) {
      Serial.printf("HOMIE CONFIG:%d: %s\n",num_nodes++, node[HOMIE_NAME].as<JsonString>().c_str());
    }
  }
  node_config_json[HOMIE_NODES] = homie_yml_config[HOMIE_NODES]; // store the nodes in a JsonArray for easy access la
  for (JsonObject n : node_config_json[HOMIE_NODES].as<JsonArray>()) {
    if (jsonExists(n[HOMIE_NAME])) {
      Serial.printf("HOMIE CONFIG NODE:%s\n",n[HOMIE_NAME].as<JsonString>().c_str());
    }
  }
  /* debug
  for(JsonObject::iterator it = config_json.begin(); it != config_json.end(); ++it) {
    //YAML config supports string and integer values. null converts to empty string
    if (!(nullptr == it->value())) { 
      if (it->value().is<const char*>()) {
        Serial.printf(" CONFIG: key '%s' val: '%s'\n", it->key().c_str(), it->value().as<const char*>());
      } else if (it->value().is<int>()) {
        Serial.printf(" CONFIG: key '%s' val: %d\n", it->key().c_str(), it->value().as<uint16_t>());
      }
    } else {
      Serial.printf("conf: key '%s' value is NULL\n", it->key().c_str());
    }
  }*/
 return true;

}

bool has_mqtt_broker() {
  return mqtt_host[0] != '\0';
}

bool mqtt_is_secure() {
  return mqtt_port == MQTT_SECURE_PORT;
}

void mqtt_build_cert_path(const char* hostname, char* out, size_t out_len) {
  // Convert "mqtt.example.com" -> "/certs/mqttExampleCom.ca"
  size_t pos = 0;
  pos += snprintf(out + pos, out_len - pos, "/certs/");

  bool capitalize_next = false;
  for (size_t i = 0; hostname[i] != '\0' && pos < out_len - 4; i++) {
    if (hostname[i] == '.') {
      capitalize_next = true;
    } else {
      if (capitalize_next && hostname[i] >= 'a' && hostname[i] <= 'z') {
        out[pos++] = hostname[i] - 32;  // uppercase
      } else {
        out[pos++] = hostname[i];
      }
      capitalize_next = false;
    }
  }
  out[pos] = '\0';
  strncat(out, ".ca", out_len - pos - 1);

  Serial.printf("TLS: Cert path for '%s' -> '%s'\n", hostname, out);
}

void mqtt_invalidate_ca_cert() {
  ca_cert_loaded = false;
  ca_cert_buffer[0] = '\0';
  Serial.println("TLS: CA cert cache invalidated");
}

const char* mqtt_load_ca_cert() {
  if (ca_cert_loaded) {
    return ca_cert_buffer;
  }

  // Build dynamic cert path from broker hostname
  char cert_path[96];
  mqtt_build_cert_path(mqtt_host, cert_path, sizeof(cert_path));

  if (!LittleFS.begin(false)) {
    Serial.println("TLS: Failed to mount LittleFS for certificate");
    return nullptr;
  }

  // Try broker-specific cert first, then uploaded cert as fallback
  const char* actual_path = nullptr;
  if (LittleFS.exists(cert_path)) {
    actual_path = cert_path;
  } else if (LittleFS.exists("/certs/uploaded-cert.ca")) {
    actual_path = "/certs/uploaded-cert.ca";
    Serial.printf("TLS: Broker cert '%s' not found, using uploaded cert\n", cert_path);
  } else {
    Serial.printf("TLS: No CA certificate found at '%s' or '/certs/uploaded-cert.ca'\n", cert_path);
    return nullptr;
  }

  File certFile = LittleFS.open(actual_path, FILE_READ);
  if (!certFile) {
    Serial.printf("TLS: Failed to open CA certificate '%s'\n", actual_path);
  
    return nullptr;
  }

  size_t certSize = certFile.size();
  if (certSize >= CA_CERT_MAX_SIZE) {
    Serial.printf("TLS: CA certificate too large (%d bytes, max %d)\n", certSize, CA_CERT_MAX_SIZE - 1);
    certFile.close();
  
    return nullptr;
  }

  size_t bytesRead = certFile.readBytes(ca_cert_buffer, certSize);
  ca_cert_buffer[bytesRead] = '\0';
  certFile.close();


  if (bytesRead != certSize) {
    Serial.printf("TLS: Certificate read incomplete (%d of %d bytes)\n", bytesRead, certSize);
    return nullptr;
  }

  ca_cert_loaded = true;
  Serial.printf("TLS: CA certificate loaded (%d bytes) from '%s'\n", bytesRead, actual_path);
  return ca_cert_buffer;
}

// --- OTA enable/disable with auto-timeout ---

bool ota_is_enabled() {
    return _ota_enabled;
}

void ota_set_enabled(bool enabled) {
    _ota_enabled = enabled;
    if (enabled) {
        _ota_enabled_at = millis();
        Serial.printf("OTA: Enabled (timeout=%d min)\n", OTA_ENABLED_TIMEOUT);
    } else {
        _ota_enabled_at = 0;
        Serial.println("OTA: Disabled");
    }
}

void ota_check_timeout() {
    if (!_ota_enabled || OTA_ENABLED_TIMEOUT == 0) return;
    unsigned long elapsed = millis() - _ota_enabled_at;
    if (elapsed >= (unsigned long)OTA_ENABLED_TIMEOUT * 60UL * 1000UL) {
        Serial.println("OTA: Auto-disabled after timeout");
        ota_set_enabled(false);
    }
}