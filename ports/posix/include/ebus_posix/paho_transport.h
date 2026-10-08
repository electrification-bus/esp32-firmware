#pragma once
// HomieTransport over the Eclipse Paho MQTT C client (MQTTClient API, callback mode).
//
// Threads. Paho runs a receive thread that calls messageArrived() and connectionLost().
// Those callbacks only copy into, or set flags on, this object. Everything else, the
// connect and reconnect, the /set dispatch, the controller hand-off and every publish and
// subscribe, runs on the thread that calls loop(): the application's main loop, which
// owns the client in the sense of the HomieTransport contract. queue_publish() is the one
// call that is safe from any thread.
//
// Storage is fixed and lives in the object (around 200 KB with the defaults): the
// inbound arena, the queue_publish() ring and the disconnected-link hold. Declare the
// transport static, not on a stack. Paho allocates per in-flight message internally,
// bounded by its in-flight window.
#include <ebus_posix/publish_hold.h>
#include <homie/controller_inbox.h>
#include <homie/homie_transport.h>
#include <homie/Property.h>
#include <atomic>
#include <mutex>
#include <stdint.h>

#ifndef EBUS_POSIX_INBOX_BYTES
#define EBUS_POSIX_INBOX_BYTES (64 * 1024)
#endif
#ifndef EBUS_POSIX_PUBLISH_QUEUE
#define EBUS_POSIX_PUBLISH_QUEUE 64
#endif

struct PahoConfig {
    const char* host = "localhost";
    int port = 1883;
    const char* client_id = nullptr;   // required
    const char* username = nullptr;    // null or empty: anonymous
    const char* password = nullptr;
    const char* will_topic = nullptr;  // null: no Last Will
    const char* will_payload = "lost";
    int keepalive_s = 60;
    int connect_timeout_s = 5;
    uint32_t reconnect_interval_ms = 2000;
};

class PahoTransport final : public HomieTransport {
 public:
    // After every successful connect: the hold has been flushed and every settable topic
    // re-subscribed. `first` is true for the first connect of this transport.
    typedef void (*connected_fn)(void* ctx, bool first);
    // An inbound message no settable claims, on the loop() thread. The signature matches
    // controller_mqtt_callback(), so a controller passes that directly.
    typedef void (*fallback_fn)(char* topic, uint8_t* payload, unsigned int length);

    PahoTransport() = default;
    ~PahoTransport();
    PahoTransport(const PahoTransport&) = delete;
    PahoTransport& operator=(const PahoTransport&) = delete;

    // Create the client. Copies every string in `config`. Does not connect.
    bool begin(const PahoConfig& config);
    void on_connected(connected_fn fn, void* ctx) { _connected_fn = fn; _connected_ctx = ctx; }
    void set_fallback(fallback_fn fn) { _fallback_fn = fn; }

    // The application's loop: connect or reconnect when due, then hand inbound messages to
    // the settable table or the fallback, then send what queue_publish() queued.
    void loop();

    // Clean DISCONNECT (the broker does not fire the will); no reconnect after it.
    void disconnect(int timeout_ms);

    // HomieTransport. publish() and subscribe(): loop() thread only. While the link is
    // down publish() holds or drops the message (see PublishHold) and returns false.
    using HomieTransport::publish;
    bool publish(const char* topic, const char* payload, int length, bool retained,
                 int qos) override;
    bool queue_publish(const char* topic, const char* payload, int length, bool retained,
                       Property* source) override;
    bool subscribe(const char* topic, int qos) override;
    bool connected() override;
    int last_error() override { return _last_error; }

    uint32_t inbound_dropped() const { return _inbound_dropped.load(); }
    uint32_t queue_dropped() const { return _queue_dropped.load(); }

 private:
    struct Queued {
        char topic[HOMIE_TOPIC_MAX + 1];
        char payload[Property::VALUE_MAX];
        int length;
        bool retained;
        Property* source;
    };

    friend struct PahoCallbacks;   // Paho's callbacks, in paho_transport.cpp
    static bool send_held(void* ctx, const char* topic, const char* payload, int length,
                          bool retained, int qos);

    bool try_connect();
    bool send(const char* topic, const char* payload, int length, bool retained, int qos);
    void hold_or_drop(const char* topic, const char* payload, int length, bool retained,
                      int qos);
    void drain_inbound();
    void drain_queue();

    void* _client = nullptr;   // MQTTClient
    char _host[256] = {0};
    char _client_id[HOMIE_DEVICE_ID_MAX + 1] = {0};
    char _username[128] = {0};
    char _password[128] = {0};
    char _will_topic[HOMIE_TOPIC_MAX + 1] = {0};
    char _will_payload[32] = {0};
    PahoConfig _config;

    connected_fn _connected_fn = nullptr;
    void* _connected_ctx = nullptr;
    fallback_fn _fallback_fn = nullptr;

    // Set by a connect that flushed and re-subscribed; cleared by connectionLost (Paho's
    // thread) or a failed send. publish() goes to the wire only while it is set.
    std::atomic<bool> _link_ready{false};
    bool _ever_connected = false;
    bool _was_up = false;
    bool _stopped = false;
    bool _attempted = false;
    uint32_t _next_attempt_ms = 0;
    int _last_error = 0;

    // Paho's thread pushes, loop() pops; both under _inbox_lock.
    std::mutex _inbox_lock;
    uint8_t _inbox_arena[EBUS_POSIX_INBOX_BYTES];
    ControllerInbox _inbox{_inbox_arena, sizeof(_inbox_arena)};
    std::atomic<uint32_t> _inbound_dropped{0};
    char _rx_topic[HOMIE_TOPIC_MAX + 1];
    char _rx_payload[MAX_DATA_LEN];

    // queue_publish() ring: any thread pushes, loop() pops; under _queue_lock.
    std::mutex _queue_lock;
    Queued _queue[EBUS_POSIX_PUBLISH_QUEUE];
    int _queue_head = 0;
    int _queue_count = 0;
    std::atomic<uint32_t> _queue_dropped{0};

    PublishHold _hold;   // loop() thread only
};
