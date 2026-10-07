#include <platform/config.h>
#include <Arduino.h>
#include <platform/log_stream.h>
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
// mDNS. mdns_enabled is the master switch: false means no mDNS at all (no MDNS.begin(),
// no <device id>.local claim, no querier, no service records). With it on, two gates
// split the work, and their default is BUILD-CONDITIONAL; data/config.yml overrides any of
// the three at runtime, and omitting the keys there leaves these defaults in force.
// On arduino-esp32 3.x the <device id>.local hostname is claimed whenever mdns_enabled is
// on, whatever the gates say (see setup_mdns()).
//
// The version test is NOT incidental. Per doc/MDNS.md the responder crash is a null-deref
// inside the PRECOMPILED ESP-IDF mdns component shipped with arduino-esp32 2.0.x
// (_mdns_remove_scheduled_answer <- mdns_parse_packet), triggered by inbound mDNS packets
// and unfixable app-side. arduino-esp32 3.x carries the upstream rewrite. So every 2.0.x
// env keeps both gates off, and every 3.x env advertises, WiFi-AP builds included.
//
// Discovery defaults ON for 3.x Ethernet builds and OFF for WiFi-AP builds.
bool mdns_enabled = true;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
#ifdef CONFIG_WIFI_AP
bool mdns_discovery = false;
#else
bool mdns_discovery = true;
#endif
bool mdns_advertise = true;
#else
bool mdns_discovery = false;
bool mdns_advertise = false;
#endif
// WiFi STA (config.yml wifi.enabled). WiFi-AP builds have no other uplink, so STA is on;
// Ethernet builds leave the WiFi stack (about 34 KB of heap) down unless asked for it,
// even when wifi.ssid is set. See setup_wifi_sta().
#ifdef CONFIG_WIFI_AP
bool wifi_sta_enabled = true;
#else
bool wifi_sta_enabled = false;
#endif
uint16_t log_tcp_port = LOG_TCP_PORT_DEFAULT;   // serial-over-TCP console; 0 = off
char mqtt_username[64] = {0};
char mqtt_password[64] = {0};
char device_id_prefix[32] = {0};

// Device info for mDNS advertisement. manufacturer and model are required _device-info
// TXT keys, so an empty configured value falls back to these defaults.
#define DEVICE_MANUFACTURER_DEFAULT "unknown"
#define DEVICE_MODEL_DEFAULT        "ESP32"
char device_name[32] = {0};
char device_type[HOMIE_TYPE_MAXLEN] = {0};   // eBus device types up to 41 chars
char device_manufacturer[32] = DEVICE_MANUFACTURER_DEFAULT;
char device_model[32] = DEVICE_MODEL_DEFAULT;
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

// Default; overridden by mqtt_mode key in config.yml. CONFIGURED_ONLY is the coherent
// default given mDNS defaults off: use the configured broker, no querier
// needed. A discovery mode auto-enables the querier (see parse_config coherence check).
MqttMode mqtt_mode = MQTT_MODE_CONFIGURED_ONLY;

// Default secure-mqtt, then mqtt; overridden by mqtt.discover in config.yml.
BrokerDiscoveryList mqtt_discover = {2, {BROKER_SVC_SECURE_MQTT, BROKER_SVC_MQTT}};

const char* UNIQUE_ID = "";
char device_id[MAX_DEVICE_ID_CHARS] = {0};

const char* platform_config_file = "/config.yml";
const char* homie_config_file = "/homie.yml";
const char* homie_config_file_backup = "/homie_config_from_SD.yml";
const char* config_file_backup = "/config_from_SD.yml";

// TLS certificate buffer - sized for typical CA certs (up to 4KB). Heap-allocated
// on first use rather than a static array — see Device.cpp's _description_json
// comment for why (arduino-esp32 3.x/pioarduino early-boot static-footprint threshold).
#define CA_CERT_MAX_SIZE 4096
static char* ca_cert_buffer = nullptr;
static bool ca_cert_loaded = false;

static char* ca_cert_buf() {
  if (ca_cert_buffer == nullptr) {
    ca_cert_buffer = new char[CA_CERT_MAX_SIZE];
    ca_cert_buffer[0] = '\0';
  }
  return ca_cert_buffer;
}

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
// Device-id template from device.yml `device.id`, e.g. "${chip_id}" or "bess-${chip_id}".
// Resolved at boot in generateDeviceID(). Empty -> "${chip_id}". An optional
// `device.id_prefix` is prepended to the result there.
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
  char resolved[MAX_DEVICE_ID_CHARS] = {0};
  resolve_device_id(device_id_template, chip_id, device_name, resolved, sizeof(resolved));

  // device.id_prefix (optional) is a DEPLOYMENT-scoped segment prepended to the resolved
  // id: "lab1" + "ebus-a1b2c3d4e5f6" -> "lab1-ebus-a1b2c3d4e5f6". It exists so a site or
  // developer can namespace a fleet from config-overlay.yml without editing the tracked
  // `id:` template in device.yml. Each segment is sanitized INDEPENDENTLY and then joined
  // with a hyphen — sanitizing after joining is not equivalent, since a run of hyphens
  // across the seam would collapse (same rule as make_homie_child_id()).
  if (device_id_prefix[0] != '\0') {
    char prefix_id[sizeof(device_id_prefix)] = {0};
    sanitize_homie_id(device_id_prefix, prefix_id, sizeof(prefix_id));
    if (prefix_id[0] != '\0') {
      int n = snprintf(device_id, sizeof(device_id), "%s-%s", prefix_id, resolved);
      if (n >= (int)sizeof(device_id)) {
        // A cut at the seam or inside the id can leave a trailing hyphen, which Homie ids
        // must not have.
        size_t len = strlen(device_id);
        while (len > 0 && device_id[len - 1] == '-') device_id[--len] = '\0';
        Serial.printf("CONFIG: WARNING device id truncated to '%s' (prefix + template "
                      "exceeds %u chars)\n", device_id, (unsigned)(sizeof(device_id) - 1));
      }
    } else {
      // Every character was illegal (e.g. "!!!"); a silent drop would be worse than a note.
      Serial.printf("CONFIG: WARNING device.id_prefix '%s' has no Homie-legal characters "
                    "— ignoring it\n", device_id_prefix);
      snprintf(device_id, sizeof(device_id), "%s", resolved);
    }
  } else {
    snprintf(device_id, sizeof(device_id), "%s", resolved);
  }

  // A device-id is an OPAQUE topic-level identifier — consumers use $type + info for
  // identity, never parse the id. If the template carries no per-unit uniqueness source,
  // warn: two identical units would collide on the broker. A prefix does NOT count — it is
  // deployment-scoped, identical across every unit that shares it.
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
  // Keep the passphrase this config produced, so a later boot with no readable config
  // can still authorize a filesystem re-push (see setup_recovery() in main.cpp).
  portal_prefs_save_recovery_passphrase(device_passphrase);

  // Emit MAC addresses for debugging/identification
  Serial.printf("CONFIG: Ethernet MAC: %s\n", get_eth_mac());
  Serial.printf("CONFIG: WiFi MAC: %s\n", get_wlan_mac());

  return true;
}

// ── data/config.yml key access ────────────────────────────────────────────────
//
// config.yml carries device/wifi/mqtt/log/mdns SECTIONS at the top level, mirroring
// device.yml key-for-key (mqtt: { host: ... }). A hand-maintained file mid-conversion (see
// doc/HandCodedDevice.md) may still use the FLAT form (mqtt_host: ...), so both are read:
// section first, flat key as fallback. The flat fallback can be dropped once no
// hand-maintained config carries one.
static JsonVariant cfg_value(const char* section, const char* key, const char* flat_key) {
  JsonVariant sec = config_json[section].as<JsonVariant>();
  if (!sec.isNull()) {
    JsonVariant v = sec[key].as<JsonVariant>();
    if (!v.isNull()) return v;
  }
  return config_json[flat_key].as<JsonVariant>();
}

// Copy a string value into a fixed buffer, leaving it untouched when the key is absent.
static void cfg_copy_str(const char* section, const char* key, const char* flat_key,
                         char* dest, size_t dest_size) {
  JsonVariant v = cfg_value(section, key, flat_key);
  if (v.is<JsonString>()) {
    strncpy(dest, v.as<JsonString>().c_str(), dest_size - 1);
    dest[dest_size - 1] = '\0';
  }
}

// True when a config key names a secret ("password" or "passphrase", any case).
static bool is_secret_key(const char* key) {
  char lower[64];
  size_t i = 0;
  for (; key[i] != '\0' && i < sizeof(lower) - 1; i++) lower[i] = tolower((unsigned char)key[i]);
  lower[i] = '\0';
  return strstr(lower, "password") != nullptr || strstr(lower, "passphrase") != nullptr;
}

// Replace every secret value, at any nesting level, with "***".
static void redact_secrets(JsonVariant v) {
  if (v.is<JsonObject>()) {
    for (JsonPair kv : v.as<JsonObject>()) {
      if (is_secret_key(kv.key().c_str())) kv.value().set("***");
      else redact_secrets(kv.value());
    }
  } else if (v.is<JsonArray>()) {
    for (JsonVariant e : v.as<JsonArray>()) redact_secrets(e);
  }
}

bool loadPlatformConfiguration() {
  File file;

  // The config.yml in the LittleFS image, else the backup file if one was put there.
  if (LittleFS.exists(platform_config_file)) {
    file = LittleFS.open(platform_config_file, FILE_READ);
    Serial.printf("CONFIG: LOAD from LittleFS default (%s)\n", platform_config_file);
  } else if (LittleFS.exists(config_file_backup)) {
    file = LittleFS.open(config_file_backup, FILE_READ);
    Serial.printf("CONFIG: LOAD from LittleFS backup (%s)\n", config_file_backup);
  }
  if (!file) {
    Serial.println("CONFIG: NO LittleFS CONFIG (default or backup)");
    return false;
  }
  // Parse into the file-static document, NOT a local: config_json below is a JsonObject
  // referencing it, and get_config() hands that reference out. A local would be destroyed
  // on return and leave both dangling.
  JsonDocument& yml_config = platform_config_json;
  yml_config.clear();
  DeserializationError error = deserializeYml( yml_config, file ); // convert yaml to json
  file.close();
  if (error) {
    Serial.printf("***** CONFIG: FAILED to read config: code:'%d'\n", error.code());
    return false;
  }
  Serial.println(F("CONFIG: DESERIALIZE OK"));
  {
    // Serial is teed to the unauthenticated TCP log console, so dump a redacted copy.
    JsonDocument redacted = yml_config;
    redact_secrets(redacted.as<JsonVariant>());
    serializeJsonPretty(redacted, Serial);
    Serial.println();
  }

  //TODO keys to #define header
  // Sections live at the TOP LEVEL, like data/homie.yml.
  config_json = yml_config.as<JsonObject>();

  // Safely copy string values with null checks. Each read takes the section form first and
  // falls back to the legacy flat key — see cfg_value() above.
  cfg_copy_str("wifi", "ssid",     "wifi_ssid",     wifi_ssid,     sizeof(wifi_ssid));
  cfg_copy_str("wifi", "password", "wifi_password", wifi_password, sizeof(wifi_password));

  cfg_copy_str("mqtt", "host", "mqtt_host", mqtt_host, sizeof(mqtt_host));

  JsonVariant v_mqtt_port = cfg_value("mqtt", "port", "mqtt_port");
  if (v_mqtt_port.is<uint16_t>()) {
    mqtt_port = v_mqtt_port.as<uint16_t>();
  }

  JsonVariant v_mqtt_qos = cfg_value("mqtt", "qos", "mqtt_qos");
  if (v_mqtt_qos.is<int>()) {
    int q = v_mqtt_qos.as<int>();
    if (q >= 0 && q <= 2) mqtt_qos = (uint8_t)q;   // QoS for retained Homie publications (C4)
    else Serial.printf("CONFIG: ignoring invalid mqtt_qos=%d (must be 0-2)\n", q);
  }

  JsonVariant v_wifi_enabled = cfg_value("wifi", "enabled", "wifi_enabled");
  if (v_wifi_enabled.is<bool>()) {
    wifi_sta_enabled = v_wifi_enabled.as<bool>();
  }

  // mDNS. `enabled` sets the master switch and both gates; the granular keys then override
  // the gates, except that enabled=false wins over them: no mDNS at all.
  JsonVariant v_mdns_enabled = cfg_value("mdns", "enabled", "mdns_enabled");
  if (v_mdns_enabled.is<bool>()) {
    bool en = v_mdns_enabled.as<bool>();
    mdns_enabled = en;
    mdns_discovery = en;
    mdns_advertise = en;
  }
  JsonVariant v_mdns_discovery = cfg_value("mdns", "discovery", "mdns_discovery");
  if (v_mdns_discovery.is<bool>()) {
    mdns_discovery = v_mdns_discovery.as<bool>();
  }
  JsonVariant v_mdns_advertise = cfg_value("mdns", "advertise", "mdns_advertise");
  if (v_mdns_advertise.is<bool>()) {
    mdns_advertise = v_mdns_advertise.as<bool>();
  }
  if (!mdns_enabled && (mdns_discovery || mdns_advertise)) {
    Serial.println("CONFIG: mdns.enabled=false overrides mdns.discovery/mdns.advertise; mDNS stays off");
    mdns_discovery = false;
    mdns_advertise = false;
  }

  // Serial-over-TCP console port; 0 disables the listener (see log_stream.h).
  JsonVariant v_log_port = cfg_value("log", "tcp_port", "log_tcp_port");
  if (v_log_port.is<uint16_t>()) {
    log_tcp_port = v_log_port.as<uint16_t>();
  }

  cfg_copy_str("mqtt", "username", "mqtt_username", mqtt_username, sizeof(mqtt_username));
  cfg_copy_str("mqtt", "password", "mqtt_password", mqtt_password, sizeof(mqtt_password));

  cfg_copy_str("device", "id_prefix", "device_id_prefix", device_id_prefix, sizeof(device_id_prefix));

  // device_id is a TEMPLATE (e.g. "{chip_id}", "bess-{chip_id}"); resolved at boot in
  // generateDeviceID(). Empty -> "{chip_id}".
  cfg_copy_str("device", "id", "device_id", device_id_template, sizeof(device_id_template));

  // device_name is loaded from homie.yml (name field), not config.yml

  // device.yml carries both `device_type:` and `type:`; the generator prefers device_type
  // and emits it under that name, so try it first and fall back to `type:` for an overlay
  // or hand-written config that only set the short form.
  cfg_copy_str("device", "device_type", "device_type", device_type, sizeof(device_type));
  if (device_type[0] == '\0') {
    cfg_copy_str("device", "type", "device_type", device_type, sizeof(device_type));
  }

  cfg_copy_str("device", "manufacturer",  "manufacturer",  device_manufacturer, sizeof(device_manufacturer));
  cfg_copy_str("device", "model",         "model",         device_model,        sizeof(device_model));
  // generate writes "" for an unset key, which would replace the defaults.
  if (device_manufacturer[0] == '\0') strcpy(device_manufacturer, DEVICE_MANUFACTURER_DEFAULT);
  if (device_model[0] == '\0') strcpy(device_model, DEVICE_MODEL_DEFAULT);
  cfg_copy_str("device", "serial_number", "serial_number", device_serial,       sizeof(device_serial));

  cfg_copy_str("device", "firmware_name",    "firmware_name",    firmware_name,    sizeof(firmware_name));
  cfg_copy_str("device", "firmware_version", "firmware_version", firmware_version, sizeof(firmware_version));
  cfg_copy_str("device", "firmware_url",     "firmware_url",     firmware_url,     sizeof(firmware_url));
  cfg_copy_str("device", "fs_url",            "fs_url",           fs_url,           sizeof(fs_url));

  cfg_copy_str("device", "homie_role",        "homie_role",        homie_role,        sizeof(homie_role));
  cfg_copy_str("device", "device_passphrase", "device_passphrase", device_passphrase, sizeof(device_passphrase));

  JsonVariant v_mqtt_mode = cfg_value("mqtt", "mode", "mqtt_mode");
  if (v_mqtt_mode.is<JsonString>()) {
    const char* mode_str = v_mqtt_mode.as<JsonString>().c_str();
    if (strcmp(mode_str, "configured-only") == 0) {
      mqtt_mode = MQTT_MODE_CONFIGURED_ONLY;
    } else if (strcmp(mode_str, "discovery-with-fallback") == 0) {
      mqtt_mode = MQTT_MODE_DISCOVERY_WITH_FALLBACK;
    } else {
      mqtt_mode = MQTT_MODE_DISCOVERY_ONLY;
    }
    Serial.printf("CONFIG: mqtt_mode=%s\n", mode_str);
  }

  // Broker service types to browse, in priority order. generate validates the list; a
  // hand-written config.yml gets each bad entry skipped, and the default if none is left.
  JsonVariant v_discover = cfg_value("mqtt", "discover", "mqtt_discover");
  if (v_discover.is<JsonArray>()) {
    BrokerDiscoveryList list;
    broker_discovery_clear(&list);
    for (JsonVariant e : v_discover.as<JsonArray>()) {
      const char* name = e.is<const char*>() ? e.as<const char*>() : "";
      BrokerDiscoveryAdd r = broker_discovery_add(&list, name);
      if (r == BROKER_ADD_UNKNOWN) {
        Serial.printf("CONFIG: mqtt.discover: ignoring unknown service '%s'\n", name);
      } else if (r == BROKER_ADD_UNSUPPORTED) {
        Serial.printf("CONFIG: mqtt.discover: ignoring '%s' (WebSocket transport not supported)\n", name);
      } else if (r == BROKER_ADD_DUPLICATE) {
        Serial.printf("CONFIG: mqtt.discover: ignoring duplicate '%s'\n", name);
      }
    }
    if (list.count > 0) {
      mqtt_discover = list;
    } else {
      Serial.println("CONFIG: mqtt.discover has no usable service, keeping secure-mqtt, mqtt");
    }
  }

  // Coherence: a discovery mqtt_mode locates the broker via mDNS, so it
  // needs the querier. If discovery is requested but mdns_discovery is off, enable it —
  // otherwise no broker can ever be found and the device never connects.
  // A discovery mode also wins over mdns.enabled=false: the querier starts (and with it,
  // through the Arduino MDNS API, the hostname claim), but no service records.
  if (mqtt_mode != MQTT_MODE_CONFIGURED_ONLY && !mdns_discovery) {
    if (!mdns_enabled) {
      mdns_enabled = true;
      Serial.println("CONFIG: mqtt_mode needs discovery, which overrides mdns.enabled=false: "
                     "starting the mDNS querier (claims the .local hostname, no service records)");
    } else {
      Serial.println("CONFIG: mqtt_mode needs discovery — enabling mdns_discovery");
    }
    mdns_discovery = true;
  }

  return true;
}

bool loadHomieConfiguration() {
  File file;
  // The homie.yml in the LittleFS image, else the backup file if one was put there.
  if (LittleFS.exists(homie_config_file)) {
    file = LittleFS.open(homie_config_file, FILE_READ);
    Serial.printf("HOMIE CONFIG: LOAD from LittleFS default (%s)\n", homie_config_file);
  } else if (LittleFS.exists(homie_config_file_backup)) {
    file = LittleFS.open(homie_config_file_backup, FILE_READ);
    Serial.printf("HOMIE CONFIG: LOAD from LittleFS backup (%s)\n", homie_config_file_backup);
  }
  if (!file) {
    Serial.println("HOMIE CONFIG: NO LittleFS CONFIG (default or backup)");
    return false;
  }
  JsonDocument homie_yml_config;
  DeserializationError error = deserializeYml( homie_yml_config, file ); // convert yaml to json
  file.close();
  if (error) {
    Serial.printf("***** HOMIE CONFIG: FAILED to read config: code:'%d'\n", error.code());
    return false;
  }
  Serial.println(F("HOMIE CONFIG: DESERIALIZE OK"));

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
  Serial.printf("HOMIE CONFIG: DEVICE homie %s name %s type %s\n", homie_root[HOMIE_HOMIE].as<const char*>(), homie_root[HOMIE_NAME].as<const char*>(),homie_root[HOMIE_TYPE].as<const char*>());

  /*** process the nodes */
  int num_nodes = 0;
  for (JsonObject node : homie_yml_config[HOMIE_NODES].as<JsonArray>()) {
    if (jsonExists(node[HOMIE_NAME])) {
      Serial.printf("HOMIE CONFIG:%d: %s\n",num_nodes++, node[HOMIE_NAME].as<JsonString>().c_str());
    }
  }
  node_config_json[HOMIE_NODES] = homie_yml_config[HOMIE_NODES]; // store the nodes in a JsonArray for easy access later
  for (JsonObject n : node_config_json[HOMIE_NODES].as<JsonArray>()) {
    if (jsonExists(n[HOMIE_NAME])) {
      Serial.printf("HOMIE CONFIG NODE:%s\n",n[HOMIE_NAME].as<JsonString>().c_str());
    }
  }
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
  ca_cert_buf()[0] = '\0';
  Serial.println("TLS: CA cert cache invalidated");
}

const char* mqtt_load_ca_cert() {
  if (ca_cert_loaded) {
    return ca_cert_buf();
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

  size_t bytesRead = certFile.readBytes(ca_cert_buf(), certSize);
  ca_cert_buf()[bytesRead] = '\0';
  certFile.close();


  if (bytesRead != certSize) {
    Serial.printf("TLS: Certificate read incomplete (%d of %d bytes)\n", bytesRead, certSize);
    return nullptr;
  }

  ca_cert_loaded = true;
  Serial.printf("TLS: CA certificate loaded (%d bytes) from '%s'\n", bytesRead, actual_path);
  return ca_cert_buf();
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