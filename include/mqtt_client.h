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

#define MQTTPublish_rootrate 10000

class NodeEntity; // Forward declaration

typedef void (Property::*property_settable_callback_t)(const char*, const char*);
typedef void (NodeEntity::*entity_settable_callback_t)(Property*);

struct subscribed_settable_property_t {
    char topic[64];
    property_settable_callback_t property_callback;
    entity_settable_callback_t entity_callback;
    Property* instance; // store the instance to call the member function on
    NodeEntity* entity; // Pointer to the associated hardware entity
};



void loop_mqtt();
void setup_mqtt();
void subscribe_for_callbacks(const char* topic, property_settable_callback_t cb, Property* instance);
void entity_subscribe_for_callbacks(const char* topic, entity_settable_callback_t cb, NodeEntity* entity);
void subscriber_callback(char* topic, uint8_t* payload, unsigned int length);
void mqtt_publish_comma_sep_colon_delim(const char* subtopic, const char * data);

void mqtt_publish(const char * data);
void mqtt_restart();
boolean mqtt_connected();
extern PubSubClient mqtt_client;