#pragma once
#include <Arduino.h>
#include <MQTT.h>
#include <homie/homie_limits.h>
#include <homie/homie_transport.h>   // HomieTransport, MAX_DATA_LEN, homie_qos()
// Property is used here ONLY as a pointer and in pointer-to-member typedefs, both of
// which an incomplete type satisfies. Forward-declaring instead of including breaks a
// real cycle: homie/Property.h includes this header, so including it back made
// Property.h non-self-contained — putting `#include <homie/Property.h>` first in a new
// file failed with "'Property' has not been declared". It only ever worked because every
// existing translation unit happened to reach this header first.
class Property;

#define MQTTPublish_rootrate 10000

class NodeEntity; // Forward declaration

// Validates and stores a /set payload on the Homie property (Property::store_set_payload).
typedef bool (Property::*property_settable_callback_t)(const char* payload);
// Driver settable callbacks return true when they accepted (applied) the value and false
// when they refused it. Only an accepted value is published; a refused one is put back.
typedef bool (NodeEntity::*entity_settable_callback_t)(Property*);

// Per-settable handler (D3): a plain function pointer + opaque context — the STL-free,
// heap-free callback idiom (no std::function, no lambda). The author registers a static
// trampoline and passes `this` as ctx to recover instance state. value is the decoded
// /set payload. Returns true to accept the value, false to refuse it.
typedef bool (*settable_handler_t)(void* ctx, const char* value);

// EVERY member needs a default initializer. The dispatch tests each callback pointer for
// null before using it, so an indeterminate one is not "unused garbage" — it is a branch
// taken into a junk pointer-to-member. Three functions register entries here and each
// sets only the fields it cares about, so whatever the others are is what dispatch sees.
struct subscribed_settable_property_t {
    char topic[HOMIE_TOPIC_MAX + 1] = {0};
    property_settable_callback_t property_callback = nullptr;
    entity_settable_callback_t entity_callback = nullptr;
    Property* instance = nullptr; // store the instance to call the member function on
    NodeEntity* entity = nullptr; // Pointer to the associated hardware entity
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

// Set a settable property from INSIDE the firmware, with no broker round-trip: the value
// is delivered straight to the same handler an inbound /set would reach, so it still works
// while MQTT is down. `topic` is the full /set topic
// ("homie/5/<device-id>/<node>/<property>/set"). Returns false when no settable property
// is registered for that topic (a config error — wrong node/property id — not a
// transient one).
//
// This is how one component actuates another: address the target as <node>/<property>,
// the same pair an MQTT controller would publish to, rather than holding a pointer to it.
// A pointer can only be wired from a hand-coded src/homie.cpp (the generator cannot emit
// cross-node links), which is why examples/bms still carries setContactor().
//
// Call it from the main task only: it runs the same dispatch as an inbound /set, which
// snapshots and restores the property's value without a lock. A true return means the
// set was delivered, not that the driver accepted it.
bool deliver_local_set(const char* topic, const char* value);

// Callback type for forwarding unhandled MQTT messages (used by controller)
typedef void (*mqtt_fallback_callback_t)(char* topic, uint8_t* payload, unsigned int length);

// Register a fallback callback for messages not handled by property subscriptions
void mqtt_set_fallback_callback(mqtt_fallback_callback_t callback);

// Callback type for MQTT reconnection (to re-publish state)
typedef void (*mqtt_reconnect_callback_t)();

// Register a callback to be called after MQTT reconnection
void mqtt_set_reconnect_callback(mqtt_reconnect_callback_t callback);

void mqtt_restart();
boolean mqtt_connected();


// Thread-safe MQTT publish queue (for publishing from other tasks/cores)
void mqtt_queue_init();
bool mqtt_queue_publish(const char* topic, const char* payload, bool retained = false);
// Length-aware variant — publishes exactly `length` bytes (carries a single 0x00 byte
// for the Homie empty-string value; a zero-length payload would retract the topic). C3.
// `source` is set only by Property::publish_queued(), which counts the message in
// flight; the queue calls its queued_publish_done() exactly once, when the client sends
// the message or it is dropped.
bool mqtt_queue_publish(const char* topic, const char* payload, int length, bool retained,
                        Property* source = nullptr);

extern MQTTClient mqtt_client;

// HomieTransport over an arduino-mqtt client. queue_publish() always goes through the
// firmware's publish queue above, which mqtt_client drains, whichever client this wraps.
class MqttClientTransport final : public HomieTransport {
 public:
    explicit MqttClientTransport(MQTTClient& client) : _client(client) {}

    using HomieTransport::publish;
    bool publish(const char* topic, const char* payload, int length, bool retained,
                 int qos) override {
        return _client.publish(topic, payload, length, retained, qos);
    }
    bool queue_publish(const char* topic, const char* payload, int length, bool retained,
                       Property* source) override {
        return mqtt_queue_publish(topic, payload, length, retained, source);
    }
    bool subscribe(const char* topic, int qos) override { return _client.subscribe(topic, qos); }
    bool connected() override { return _client.connected(); }
    int last_error() override { return (int)_client.lastError(); }

 private:
    MQTTClient& _client;
};

// The transport the firmware's Homie devices, settable table and controller use.
extern MqttClientTransport mqtt_transport;
