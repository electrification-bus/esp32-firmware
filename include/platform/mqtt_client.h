#pragma once
#include <Arduino.h>
//#include <PubSubClient.h>   // MQTT https://github.com/knolleary/pubsubclient PlatformIO lib: 89
#include <MQTT.h>
#include <homie/Property.h>

#ifndef MAX_DATA_LEN
#define MAX_DATA_LEN 8192 //+2048
#endif

#ifndef MQTT_TIMEOUT
#define MQTT_TIMEOUT 3
#endif

#define MQTTPublish_rootrate 10000

class NodeEntity; // Forward declaration

typedef void (Property::*property_settable_callback_t)(const char*, const char*);
typedef void (NodeEntity::*entity_settable_callback_t)(Property*);

// Per-settable handler (D3): a plain function pointer + opaque context — the STL-free,
// heap-free callback idiom (no std::function, no lambda). The author registers a static
// trampoline and passes `this` as ctx to recover instance state. value is the decoded
// /set payload.
typedef void (*settable_handler_t)(void* ctx, const char* value);

struct subscribed_settable_property_t {
    char topic[128];
    property_settable_callback_t property_callback;
    entity_settable_callback_t entity_callback;
    Property* instance; // store the instance to call the member function on
    NodeEntity* entity; // Pointer to the associated hardware entity
    settable_handler_t handler = nullptr;   // D3: per-property fn-ptr handler
    void* handler_ctx = nullptr;            // D3: opaque context for handler
};



void loop_mqtt();
void setup_mqtt();
void subscribe_for_callbacks(const char* topic, property_settable_callback_t cb, Property* instance);
void entity_subscribe_for_callbacks(const char* topic, entity_settable_callback_t cb, NodeEntity* entity);
// D3: register a per-property fn-ptr handler (+context) for a /set topic.
void subscribe_settable_handler(const char* topic, settable_handler_t handler, void* ctx);
void subscriber_callback(MQTTClient* client, char topic[], char bytes[], int length);
void mqtt_publish_comma_sep_colon_delim(const char* subtopic, const char * data);

// Callback type for forwarding unhandled MQTT messages (used by controller)
typedef void (*mqtt_fallback_callback_t)(char* topic, uint8_t* payload, unsigned int length);

// Register a fallback callback for messages not handled by property subscriptions
void mqtt_set_fallback_callback(mqtt_fallback_callback_t callback);

// Callback type for MQTT reconnection (to re-publish state)
typedef void (*mqtt_reconnect_callback_t)();

// Register a callback to be called after MQTT reconnection
void mqtt_set_reconnect_callback(mqtt_reconnect_callback_t callback);

void mqtt_publish(const char * data);
void mqtt_restart();
boolean mqtt_connected();


// QoS for RETAINED Homie publications (property values + $state/$description).
// Homie 5 recommends QoS 2 (SHOULD), so it defaults to 2 but is configurable via the
// `mqtt_qos` config key. Non-retained publications and /set are fixed at QoS 0 per the
// spec (a hard requirement), so homie_qos() returns 0 for them regardless. (C4)
extern uint8_t mqtt_qos;
inline int homie_qos(bool retained) { return retained ? (int)mqtt_qos : 0; }

// Thread-safe MQTT publish queue (for publishing from other tasks/cores)
void mqtt_queue_init();
bool mqtt_queue_publish(const char* topic, const char* payload, bool retained = false);
// Length-aware variant — publishes exactly `length` bytes (carries a single 0x00 byte
// for the Homie empty-string value; a zero-length payload would retract the topic). C3.
bool mqtt_queue_publish(const char* topic, const char* payload, int length, bool retained);

//extern PubSubClient mqtt_client;
extern MQTTClient mqtt_client;