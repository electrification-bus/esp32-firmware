#pragma once
#include <Arduino.h>
#include <MQTT.h>
#include <homie/homie_limits.h>
#include <homie/homie_transport.h>   // HomieTransport, MAX_DATA_LEN, homie_qos()
#include <homie/homie_settable.h>    // the settable table and deliver_local_set()
#include <ebus/mqtt/publish_hold.h>

#define MQTTPublish_rootrate 10000

void loop_mqtt();
void setup_mqtt();
void subscriber_callback(MQTTClient* client, char topic[], char bytes[], int length);


// Callback type for forwarding unhandled MQTT messages (used by controller)
typedef void (*mqtt_fallback_callback_t)(char* topic, uint8_t* payload, unsigned int length);

// Register a fallback callback for messages not handled by property subscriptions
void mqtt_set_fallback_callback(mqtt_fallback_callback_t callback);

// Called after every connect, once the publishes held while the link was down have been
// sent and every settable topic re-subscribed. `held_lost` is true when a retained
// publish made while the link was down could not be held (a payload over
// EBUS_MQTT_HOLD_PAYLOAD_MAX, such as a $description, or the oldest entry of a full
// hold), so the broker may not have it and the callback should publish it again.
typedef void (*mqtt_reconnect_callback_t)(bool held_lost);

void mqtt_set_reconnect_callback(mqtt_reconnect_callback_t callback);

void mqtt_restart();
boolean mqtt_connected();


// Thread-safe MQTT publish queue (for publishing from other tasks/cores). Also allocates
// the transport's publish hold.
void mqtt_queue_init();
bool mqtt_queue_publish(const char* topic, const char* payload, bool retained = false);
// Length-aware variant: publishes exactly `length` bytes (carries a single 0x00 byte for
// the Homie empty-string value; a zero-length payload would retract the topic). C3.
// When `source` is set, its queued_publish_done() is called exactly once, when the
// message is sent or dropped.
bool mqtt_queue_publish(const char* topic, const char* payload, int length, bool retained,
                        Property* source = nullptr);

extern MQTTClient mqtt_client;

// HomieTransport over an arduino-mqtt client, with ebus_mqtt's disconnected-link rules
// (lib/ebus_core/doc/mqtt.md). queue_publish() always goes through the firmware's publish
// queue above, which loop_mqtt() drains into mqtt_transport, whichever client this wraps.
//
// The link is usable only between a successful after_connect() and the next loss. While
// it is not, publish() hands the message to the hold (retained: newest per topic;
// QoS 1/2: in order; QoS 0: dropped) and returns false. Everything but queue_publish()
// runs on the task that owns the client.
class MqttClientTransport final : public HomieTransport {
 public:
    // After a connect: `held_lost` as for mqtt_reconnect_callback_t.
    typedef void (*connected_fn)(void* ctx, bool first, bool held_lost);

    explicit MqttClientTransport(MQTTClient& client) : _client(client) {}

    // The hold for publishes made while the link is down. Without one they are dropped.
    void set_hold(PublishHold* hold) { _hold = hold; }
    void on_connected(connected_fn fn, void* ctx) { _connected_fn = fn; _connected_ctx = ctx; }

    // The client lost or closed its session: publish() holds from here on.
    void link_down() { _link_ready = false; }
    // Call once the client has connected: flush the hold, re-subscribe every settable
    // topic, then call the on_connected() callback (mqtt_after_connect()). Returns false
    // when the link dropped meanwhile; the link then stays down for the next connect.
    bool after_connect();

    using HomieTransport::publish;
    using HomieTransport::queue_publish;
    bool publish(const char* topic, const char* payload, int length, bool retained,
                 int qos) override;
    bool queue_publish(const char* topic, const char* payload, int length, bool retained,
                       int qos, mqtt_publish_done_fn done, void* ctx) override;
    bool subscribe(const char* topic, int qos) override;
    bool connected() override { return _link_ready && _client.connected(); }
    int last_error() override { return (int)_client.lastError(); }

 private:
    static bool send_held(void* ctx, const char* topic, const char* payload, int length,
                          bool retained, int qos);
    static bool resubscribe_all(void* ctx);
    static void notify_connected(void* ctx, bool first);
    void hold_or_drop(const char* topic, const char* payload, int length, bool retained,
                      int qos);

    MQTTClient& _client;
    PublishHold* _hold = nullptr;
    connected_fn _connected_fn = nullptr;
    void* _connected_ctx = nullptr;
    bool _link_ready = false;
    bool _ever_connected = false;
    // A retained publish was dropped or evicted while the link was down.
    bool _held_lost = false;
    int _held_at_connect = 0;
};

// The transport the firmware's Homie devices, settable table and controller use.
extern MqttClientTransport mqtt_transport;
