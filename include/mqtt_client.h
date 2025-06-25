#pragma once
#include <Arduino.h>
#include <PubSubClient.h>   // MQTT https://github.com/knolleary/pubsubclient PlatformIO lib: 89
#include <homie/Property.h>

#ifndef MAX_DATA_LEN
#define MAX_DATA_LEN 4096
#endif

#ifndef MQTT_TIMEOUT
#define MQTT_TIMEOUT 3
#endif
typedef void (Property::*subscriber_callback_t)(const char*, const char*);

void loop_mqtt();
void setup_mqtt();
void subscribe_for_callbacks(const char* topic, subscriber_callback_t cb, Property* instance);
void subscriber_callback(char* topic, uint8_t* payload, unsigned int length);
void mqtt_publish_comma_sep_colon_delim(const char* subtopic, const char * data);

void mqtt_publish(const char * data);
void mqtt_restart();
boolean mqtt_connected();
extern PubSubClient mqtt_client;