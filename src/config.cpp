#include <config.h>
#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoYaml.h>

static JsonObject config_json;
static JsonDocument node_config_json;
String mqtt_host;
uint16_t mqtt_port;
String mqtt_username;
String mqtt_password;
String device_id_prefix;

const char* UNIQUE_ID = "";
char device_id[MAX_DEVICE_ID_CHARS] = {0};

const char* config_file = "/config.yml";
const char* config_file_backup = "/config_from_SD.json";

const bool default_value = false; // default value for property
bool current_value = default_value;
bool config_loaded = false; // prevent updates if config isn't loaded

JsonObject* get_config() {
    return &config_json;
}
JsonDocument* get_node_config() {
    return &node_config_json;
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
  return (const char*)device_id;
}

bool loadConfiguration() {
  Serial.println("CONFIG: trying SD card");
  File file;

  Serial.println("CONFIG: trying SPIFFS");
  if(!SPIFFS.begin(true)){
    Serial.println("CONFIG: An Error has occurred while mounting SPIFFS");
    return false;
  }
  //see if we have a config from the build (SPIFFS)
  if (SPIFFS.exists(config_file)) {
    file = SPIFFS.open(config_file, FILE_READ);
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
  JsonDocument config; // https://arduinojson.org/v6/assistant to compute the capacity of static (deprecated).
  DeserializationError error;
  Serial.printf("CONFIG: YAML config:'%s'\n", config_file);
  error = deserializeYml( config, file ); // convert yaml to json

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
  JsonArray conf_array = config["config"].as<JsonArray>();
  config_json = conf_array[0].as<JsonObject>();
  mqtt_host = config_json["mqtt_host"].as<JsonString>().c_str();
  mqtt_port = config_json["mqtt_port"].as<uint16_t>();
  mqtt_username = config_json["mqtt_username"].as<JsonString>().c_str();
  mqtt_password = config_json["mqtt_password"].as<JsonString>().c_str();
  device_id_prefix = config_json["device_id_prefix"].as<JsonString>().c_str();

    /*** process the nodes */
  int num_nodes = 0;
  for (JsonObject node : config["nodes"].as<JsonArray>()) {
    Serial.printf("CONFIG:%d: %s\n",num_nodes++, node["name"].as<JsonString>().c_str());
  }
  node_config_json["nodes"] = config["nodes"]; // store the nodes in a JsonArray for easy access la
  for (JsonObject n : node_config_json["nodes"].as<JsonArray>()) {
    Serial.printf("CONFIG NODE:%s\n",n["name"].as<JsonString>().c_str());
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