#include <config.h>
#include <Arduino.h>
#include <SPIFFS.h>

String UNIQUE_ID = "";

String config_file = "/homie/config.json";
String spiffs_config_file = "/config.json";
String config_file_backup = "/config_from_SD.json";

char device_id[MAX_DEVICE_ID_CHARS] = {0};

void generatefullDeviceID() {// includes OUI vendorid of the ethernet MAC inside the ESP32S2 -prefer to not use  full MACid
  uint32_t low = ESP.getEfuseMac() & 0xFFFFFFFF;
  uint32_t high = (ESP.getEfuseMac() >> 32) & 0xFFFFFFFF;
  //uint64_t fullMAC = word(low, high);
  sprintf(device_id, "%s%X%X", DEVICE_ID_PREFIX, high, low);
  Serial.printf("Device ID: %s\n", device_id);
}

void generateDeviceID() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  //Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  sprintf(device_id, "%s%02X%02X%02X", DEVICE_ID_PREFIX, mac[3], mac[4], mac[5]);
  Serial.printf("Device ID: %s\n", device_id);
}

const char* getDeviceID() {
  return (const char*)device_id;
}

bool loadConfiguration() {
  Serial.println("CONFIG: trying SD card");
  File file;
  bool loaded_from_SD = false;

  Serial.println("CONFIG: trying SPIFFS");
  if(!SPIFFS.begin(true)){
    Serial.println("CONFIG: An Error has occurred while mounting SPIFFS");
    return false;
  }
  //see if we have a config from the build (SPIFFS)
  if (SPIFFS.exists(spiffs_config_file.c_str())) {
    file = SPIFFS.open(spiffs_config_file.c_str(), FILE_READ);
    Serial.println("CONFIG: LOAD from SPIFFS default (config.json)");
  } else if (SPIFFS.exists(config_file_backup.c_str())) {
    file = SPIFFS.open(config_file_backup, FILE_READ);
    Serial.println("CONFIG: LOAD from SPIFFS backup (config_from_SD.json)");
  }
  if (!file) {
    Serial.println("CONFIG: NO SPIFFS CONFIG (overriding or backup)");
    SPIFFS.end();
    return false;
  }
    
  // Deserialize the JSON document
  JsonDocument conf; // https://arduinojson.org/v6/assistant to compute the capacity.
  DeserializationError error = deserializeJson(conf, file);
  file.close();
  if (error) {
    Serial.printf("***** CONFIG: FAILED to read config JSON: '%s'", error.c_str());
    //SD.end();
    SPIFFS.end();
    return false;
  }
  Serial.println(F("CONFIG: JSON DESERIALIZE OK"));

  //TODO keys to #define header
  const char* name = conf["name"].as<JsonString>().c_str(); // "EVPE_1.0"
  Serial.printf("CONFIG: %s\n",name);

  JsonObject foo = conf["foo"].as<JsonObject>();
  int cnt = 0;
  for (JsonObject node : foo["bar"].as<JsonArray>()) {
    int a = node["baz"].as<JsonInteger>();
    Serial.printf("CONFIG: foo.bar[%d].baz = %d\n", cnt, a);
    if (a == 0) {
      continue;
    }
    cnt++;
  }

  /*** process the nodes */
  int num_nodes = 0;
  for (JsonObject node : conf["nodes"].as<JsonArray>()) {
    Serial.printf("CONFIG:%d: %s\n",num_nodes++, node["name"].as<JsonString>().c_str());
  }

  return true;
}