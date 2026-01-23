#include <config.h>
#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoYaml.h>
#include <util/jsonUtils.h>

Device theDevice;

static JsonObject config_json;
static JsonDocument homie_config_json;
static JsonDocument platform_config_json;
static JsonDocument node_config_json;
char mqtt_host[64] = {0};
uint16_t mqtt_port;
char mqtt_username[64] = {0};
char mqtt_password[64] = {0};
char device_id_prefix[32] = {0};

const char* UNIQUE_ID = "";
char device_id[MAX_DEVICE_ID_CHARS] = {0};

const char* platform_config_file = "/config.yml";
const char* homie_config_file = "/homie.yml";
const char* homie_config_file_backup = "/homie_config_from_SD.yml";
const char* config_file_backup = "/config_from_SD.yml";

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
void generatefullDeviceID() {// includes OUI vendorid of the ethernet MAC inside the ESP32S2 -prefer to not use  full MACid
  uint32_t low = ESP.getEfuseMac() & 0xFFFFFFFF;
  uint32_t high = (ESP.getEfuseMac() >> 32) & 0xFFFFFFFF;
  //uint64_t fullMAC = word(low, high);
  sprintf(device_id, "%s%X%X", device_id_prefix, high, low);
  Serial.printf("Device ID: %s\n", device_id);
}

void generateDeviceID() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  //Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  sprintf(device_id, "%s%02X%02X%02X", device_id_prefix, mac[3], mac[4], mac[5]);
  Serial.printf("Device ID: %s\n", device_id);
}

const char* getDeviceID() {
  return device_id;
}

bool loadConfiguration() {
  Serial.println("CONFIG: trying SPIFFS");
  if(!SPIFFS.begin(true)){
    Serial.println("CONFIG: An Error has occurred while mounting SPIFFS");
    return false;
  }
  if (!loadPlatformConfiguration()) {
    Serial.println("PLATFORM CONFIG: **ERROR -- failed to load platform configuration");
    return false;
  }
  if (!loadHomieConfiguration()) {
    Serial.println("HOMIE CONFIG: **ERROR -- failed to load homie configuration");
    return false;
  }
  config_loaded = true;
  SPIFFS.end();
  return true;
}

bool loadPlatformConfiguration() {
  File file;

  //see if we have a config from the build (SPIFFS)
  if (SPIFFS.exists(platform_config_file)) {
    file = SPIFFS.open(platform_config_file, FILE_READ);
    Serial.println("CONFIG: LOAD from SPIFFS default");
  } else if (SPIFFS.exists(config_file_backup)) {
    file = SPIFFS.open(config_file_backup, FILE_READ);
    Serial.println("CONFIG: LOAD from SPIFFS backup (config_from_SD.json)");
  }
  if (!file) {
    Serial.println("CONFIG: NO SPIFFS CONFIG (overriding or backup)");
    SPIFFS.end();
    return false;
  }
  JsonObject dev_conf_obf = platform_config_json.to<JsonObject>();
  JsonDocument yml_config; // https://arduinojson.org/v6/assistant to compute the capacity of static (deprecated).
  Serial.printf("CONFIG: YAML config:'%s'\n", platform_config_file);
  DeserializationError error = deserializeYml( yml_config, file ); // convert yaml to json

  // Deserialize the JSON document
  file.close();
  if (error) {
    Serial.printf("***** CONFIG: FAILED to read config: code:'%d'\n", error.code());
    //SD.end();
    SPIFFS.end();
    return false;
  }
  Serial.println(F("CONFIG: DESERIALIZE OK"));

  //TODO keys to #define header
  JsonArray conf_array = yml_config["config"].as<JsonArray>();
  config_json = conf_array[0].as<JsonObject>();

  // Safely copy string values with null checks
  if (config_json["mqtt_host"].is<JsonString>()) {
    strncpy(mqtt_host, config_json["mqtt_host"].as<JsonString>().c_str(), sizeof(mqtt_host) - 1);
    mqtt_host[sizeof(mqtt_host) - 1] = '\0';
  }

  mqtt_port = config_json["mqtt_port"].as<uint16_t>();

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

  return true;
}

bool loadHomieConfiguration() {
  File file;
  //HOMIE specific config
  if (SPIFFS.exists(homie_config_file)) {
    file = SPIFFS.open(homie_config_file, FILE_READ);
    Serial.println("HOMIE CONFIG: LOAD from SPIFFS default");
  } else if (SPIFFS.exists(homie_config_file_backup)) {
    file = SPIFFS.open(homie_config_file_backup, FILE_READ);
    Serial.println("HOMIE CONFIG: LOAD from SPIFFS backup (homie_config_from_SD.json)");
  }
  if (!file) {
    Serial.println("HOMIE CONFIG: NO SPIFFS CONFIG (overriding or backup)");
    SPIFFS.end();
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
    SPIFFS.end();
    return false;
  }
  Serial.println(F("HOMIE CONFIG: DESERIALIZE OK"));

  /*debug
  serializeJsonPretty(yml_config, Serial);
  Serial.println();
  //the device
  for (JsonPair kv : homie_root) {
    if (kv.value().is<JsonObject>() || kv.value().is<JsonArray>())
      continue;
    Serial.printf("homie_root key: %s, value: %s\n", kv.key().c_str(), kv.value().as<const char*>());
  }*/

  JsonObject homie_conf_obf = homie_config_json.to<JsonObject>();
  JsonObject homie_root = homie_yml_config.as<JsonObject>();
  if (jsonExists(homie_root[HOMIE_HOMIE])) homie_conf_obf[HOMIE_HOMIE] = homie_root[HOMIE_HOMIE]; else {Serial.printf("CONFIG:**ERROR: MISSING '%s'\n",HOMIE_HOMIE);return false;}

  if (jsonExists(homie_root[HOMIE_NAME])) homie_conf_obf[HOMIE_NAME] = homie_root[HOMIE_NAME]; else {Serial.printf("CONFIG:**ERROR: MISSING '%s'\n",HOMIE_NAME);return false;}
  if (jsonExists(homie_root[HOMIE_TYPE])) homie_conf_obf[HOMIE_TYPE] = homie_root[HOMIE_TYPE]; else {Serial.printf("CONFIG:**ERROR: MISSING '%s'\n",HOMIE_TYPE);return false;}
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