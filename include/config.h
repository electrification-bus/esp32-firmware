#pragma once
#include <Arduino.h>
#include <ArduinoYaml.h>

#define MAX_DEVICE_ID_CHARS   32
#define HOMIE_VERSION           "5"
#define HOMIE_TOPIC_PREFIX      "homie/" HOMIE_VERSION
#define HOMIE_TOPIC_SET         "set"

extern String mqtt_host;
extern uint16_t mqtt_port;
extern String mqtt_username;
extern String mqtt_password;
extern String device_id_prefix;

void generateDeviceID();
const char* getDeviceID();
bool loadConfiguration();
JsonObject* get_config();
JsonDocument* get_node_config();