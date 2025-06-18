#pragma once
#include <Arduino.h>
#include <PubSubClient.h>   // MQTT https://github.com/knolleary/pubsubclient PlatformIO lib: 89

#ifndef MAX_DATA_LEN
#define MAX_DATA_LEN 4096
#endif

#ifndef MQTT_TIMEOUT
#define MQTT_TIMEOUT 3
#endif

void loop_mqtt();
void setup_mqtt();
void subscriber_callback(char* topic, uint8_t* payload, unsigned int length);
void mqtt_publish_comma_sep_colon_delim(const char* subtopic, const char * data);

void mqtt_publish(const char * data);
void mqtt_restart();
boolean mqtt_connected();
