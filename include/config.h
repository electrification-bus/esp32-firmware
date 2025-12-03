#pragma once
#include <Arduino.h>
#include <ArduinoYaml.h>
#include <pins.h>
#include <util/jsonUtils.h>
#include <homie/homie.h>
#include <homie/Device.h>


#define MAX_DEVICE_ID_CHARS   32 //TODO all string sizes

extern char mqtt_host[64];
extern uint16_t mqtt_port;
extern char mqtt_username[64];
extern char mqtt_password[64];
extern char device_id_prefix[32];

void generateDeviceID();
const char* getDeviceID();
bool loadConfiguration();
bool loadPlatformConfiguration();
bool loadHomieConfiguration();
JsonObject* get_config();
JsonDocument* get_node_config();
JsonDocument* get_platform_config();
JsonDocument* get_homie_config();

extern Device* theDevice;