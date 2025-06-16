#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define MAX_DEVICE_ID_CHARS   32
#define DEVICE_ID_PREFIX      "ESP32POE"

#define MQTT_TOPIC              "xyz"
#define MQTT_PUBLISH_INTERVAL   10000
#define MQTT_HOST               "192.168.1.70"//lono.local" //public.cloud.shiftr.io"  //"test.mosquitto.org"
#define MQTT_USER               ""//public"                  // leave empty for test.mosquitto.org
#define MQTT_PW                 ""//public"                  // leave empty for test.mosquitto.org   

void generateDeviceID();
const char* getDeviceID();

