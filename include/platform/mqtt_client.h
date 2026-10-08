#pragma once
#include <Arduino.h>
#include <MQTT.h>
#include <homie/homie_limits.h>
#include <homie/homie_transport.h>   // HomieTransport, MAX_DATA_LEN, homie_qos()
#include <homie/homie_settable.h>    // the settable table and deliver_local_set()

#define MQTTPublish_rootrate 10000

void loop_mqtt();
void setup_mqtt();
void subscriber_callback(MQTTClient* client, char topic[], char bytes[], int length);


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
