#pragma once
#include <Arduino.h>
#include <ArduinoYaml.h>
#include <homie/homie.h>
#include <homie/Device.h>

#define MAX_DEVICE_ID_CHARS   32 //TODO all string sizes

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
JsonDocument* get_device_config();

extern Device* theDevice;