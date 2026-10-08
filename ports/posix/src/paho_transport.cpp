// SPDX-License-Identifier: MIT
#include <ebus_posix/paho_transport.h>
#include <homie/homie_clock.h>
#include <homie/homie_log.h>
#include <homie/homie_settable.h>
#include <MQTTClient.h>
#include <stdio.h>
#include <string.h>

#define CLIENT(p) ((MQTTClient)(p))

static void copy_str(char* out, size_t size, const char* in) {
    snprintf(out, size, "%s", in ? in : "");
}

// ---- Paho's receive thread ------------------------------------------------------------
// Copy and return: no publish, no subscribe, no dispatch here (see the header).

struct PahoCallbacks {
    static int message_arrived(void* ctx, char* topic, int topic_len,
                               MQTTClient_message* m) {
        PahoTransport* self = (PahoTransport*)ctx;
        char t[HOMIE_TOPIC_MAX + 1];
        size_t tlen = topic_len > 0 ? (size_t)topic_len : strlen(topic);
        if (tlen >= sizeof(t)) {
            homie_logf("PAHO: inbound topic is %u chars, over %d; dropped\n", (unsigned)tlen,
                       HOMIE_TOPIC_MAX);
        } else if (m->payloadlen < 0 || m->payloadlen > MAX_DATA_LEN - 1) {
            homie_logf("PAHO: inbound payload on '%.*s' is %d bytes, over %d; dropped\n",
                       (int)tlen, topic, m->payloadlen, MAX_DATA_LEN - 1);
        } else {
            memcpy(t, topic, tlen);
            t[tlen] = '\0';
            std::lock_guard<std::mutex> lock(self->_inbox_lock);
            if (!self->_inbox.push(t, (const uint8_t*)m->payload, (size_t)m->payloadlen)) {
                uint32_t n = ++self->_inbound_dropped;
                homie_logf("PAHO: inbox full (%u bytes), DROPPED message on '%s' (%u "
                           "dropped)\n", (unsigned)self->_inbox.size(), t, (unsigned)n);
            }
        }
        MQTTClient_freeMessage(&m);
        MQTTClient_free(topic);
        return 1;   // consumed or deliberately dropped: never ask Paho to redeliver
    }

    static void connection_lost(void* ctx, char* cause) {
        PahoTransport* self = (PahoTransport*)ctx;
        self->_link_ready.store(false);
        homie_logf("PAHO: connection lost%s%s\n", cause ? ": " : "", cause ? cause : "");
    }
};

PahoTransport::~PahoTransport() {
    if (_client) {
        MQTTClient c = CLIENT(_client);
        MQTTClient_destroy(&c);
        _client = nullptr;
    }
}

bool PahoTransport::begin(const PahoConfig& config) {
    if (_client || !config.client_id || !config.client_id[0]) return false;
    _config = config;
    copy_str(_host, sizeof(_host), config.host);
    copy_str(_client_id, sizeof(_client_id), config.client_id);
    copy_str(_username, sizeof(_username), config.username);
    copy_str(_password, sizeof(_password), config.password);
    copy_str(_will_topic, sizeof(_will_topic), config.will_topic);
    copy_str(_will_payload, sizeof(_will_payload), config.will_payload);

    char uri[300];
    // An IPv6 literal goes in brackets.
    snprintf(uri, sizeof(uri), strchr(_host, ':') ? "tcp://[%s]:%d" : "tcp://%s:%d", _host,
             config.port);
    MQTTClient c = nullptr;
    int rc = MQTTClient_create(&c, uri, _client_id, MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTCLIENT_SUCCESS) {
        homie_logf("PAHO: create for %s failed: %s\n", uri, MQTTClient_strerror(rc));
        _last_error = rc;
        return false;
    }
    rc = MQTTClient_setCallbacks(c, this, PahoCallbacks::connection_lost,
                                 PahoCallbacks::message_arrived, nullptr);
    if (rc != MQTTCLIENT_SUCCESS) {
        homie_logf("PAHO: setCallbacks failed: %s\n", MQTTClient_strerror(rc));
        MQTTClient_destroy(&c);
        _last_error = rc;
        return false;
    }
    _client = c;
    homie_logf("PAHO: client '%s' for %s\n", _client_id, uri);
    return true;
}

// ---- the loop() thread ----------------------------------------------------------------

bool PahoTransport::send(const char* topic, const char* payload, int length, bool retained,
                         int qos) {
    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload = (void*)payload;
    msg.payloadlen = length;
    msg.qos = qos;
    msg.retained = retained ? 1 : 0;
    MQTTClient_deliveryToken token = 0;
    int rc = MQTTClient_publishMessage(CLIENT(_client), topic, &msg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        _last_error = rc;
        if (!MQTTClient_isConnected(CLIENT(_client))) _link_ready.store(false);
        return false;
    }
    return true;
}

bool PahoTransport::send_held(void* ctx, const char* topic, const char* payload, int length,
                              bool retained, int qos) {
    PahoTransport* self = (PahoTransport*)ctx;
    if (self->send(topic, payload, length, retained, qos)) return true;
    if (!MQTTClient_isConnected(CLIENT(self->_client))) return false;   // keep it held
    // Refused on a live link (a topic Paho rejects): retrying would block every flush.
    homie_logf("PAHO: held publish to '%s' refused (%d); dropped\n", topic, self->_last_error);
    return true;
}

void PahoTransport::hold_or_drop(const char* topic, const char* payload, int length,
                                 bool retained, int qos) {
    if (_stopped) return;
    _hold.hold(topic, payload, length, retained, qos);
}

bool PahoTransport::publish(const char* topic, const char* payload, int length,
                            bool retained, int qos) {
    if (!_client) return false;
    if (_link_ready.load() && send(topic, payload, length, retained, qos)) return true;
    // Down, or it went down under this send.
    if (!_link_ready.load()) hold_or_drop(topic, payload, length, retained, qos);
    return false;
}

bool PahoTransport::subscribe(const char* topic, int qos) {
    if (!_client || !_link_ready.load()) return false;
    int rc = MQTTClient_subscribe(CLIENT(_client), topic, qos);
    // MQTT 3.1.1: the granted QoS (0..2) on success, 0x80 for a refused subscription.
    if (rc < 0 || rc > 2) {
        _last_error = rc;
        return false;
    }
    return true;
}

bool PahoTransport::connected() {
    return _client && _link_ready.load() && MQTTClient_isConnected(CLIENT(_client));
}

// One connect attempt. On success: flush the hold, re-subscribe every settable topic,
// then tell the application, in that order (ebus-mqtt-client's reconnect order).
bool PahoTransport::try_connect() {
    MQTTClient c = CLIENT(_client);
    if (MQTTClient_isConnected(c)) MQTTClient_disconnect(c, 0);   // a half-up session

    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = _config.keepalive_s;
    opts.connectTimeout = _config.connect_timeout_s;
    // A clean session: every subscription is made again below, and Paho does not replay
    // in-flight messages from the old session on top of what the flush sends.
    opts.cleansession = 1;
    if (_username[0]) {
        opts.username = _username;
        opts.password = _password;
    }
    MQTTClient_willOptions will = MQTTClient_willOptions_initializer;
    if (_will_topic[0]) {
        will.topicName = _will_topic;
        will.message = _will_payload;
        will.retained = 1;
        will.qos = homie_qos(true);
        opts.will = &will;
    }

    homie_logf("PAHO: connecting to %s:%d as '%s'%s\n", _host, _config.port, _client_id,
               _username[0] ? " (authenticated)" : "");
    int rc = MQTTClient_connect(c, &opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        _last_error = rc;
        homie_logf("PAHO: connect failed: %s (%d)\n", MQTTClient_strerror(rc), rc);
        return false;
    }
    if (_will_topic[0]) homie_logf("PAHO: will '%s' = '%s'\n", _will_topic, _will_payload);

    int held = _hold.count();
    int sent = _hold.flush(send_held, this);
    homie_logf("PAHO: flushed %d of %d held publishes (%u evicted while down)\n", sent, held,
               (unsigned)_hold.evicted());
    if (sent < held) {
        homie_logln("PAHO: link dropped during the flush; the rest stays held");
        return false;
    }

    // Subscribes are made with the link marked up, so subscribe() is usable from here on.
    _link_ready.store(true);
    int subscribed = 0;
    for (int i = 0; i < settable_count(); i++) {
        if (subscribe(settable_topic(i), 0)) {   // /set is QoS 0
            subscribed++;
        } else {
            homie_logf("PAHO: subscribe '%s' failed (%d)\n", settable_topic(i), _last_error);
        }
    }
    homie_logf("PAHO: resubscribed %d of %d settable topics\n", subscribed, settable_count());
    if (!_link_ready.load()) return false;

    bool first = !_ever_connected;
    _ever_connected = true;
    homie_logf("PAHO: connected (%s)\n", first ? "first" : "reconnect");
    if (_connected_fn) _connected_fn(_connected_ctx, first);
    return true;
}

void PahoTransport::drain_inbound() {
    // Handle what is queued now; what arrives meanwhile waits for the next pass.
    size_t pending;
    {
        std::lock_guard<std::mutex> lock(_inbox_lock);
        pending = _inbox.count();
    }
    for (; pending > 0; pending--) {
        size_t length = 0;
        {
            std::lock_guard<std::mutex> lock(_inbox_lock);
            const char* t;
            const char* p;
            if (!_inbox.front(&t, &p, &length)) break;
            memcpy(_rx_topic, t, strlen(t) + 1);
            memcpy(_rx_payload, p, length + 1);   // NUL-terminated by the inbox
            _inbox.pop();
        }
        if (settable_is_registered(_rx_topic)) {
            settable_dispatch(_rx_topic, _rx_payload);
        } else if (_fallback_fn) {
            _fallback_fn(_rx_topic, (uint8_t*)_rx_payload, (unsigned)length);
        }
    }
}

void PahoTransport::drain_queue() {
    for (;;) {
        Queued item;
        {
            std::lock_guard<std::mutex> lock(_queue_lock);
            if (_queue_count == 0) return;
            item = _queue[_queue_head];
            _queue_head = (_queue_head + 1) % EBUS_POSIX_PUBLISH_QUEUE;
            _queue_count--;
        }
        bool sent = publish(item.topic, item.payload, item.length, item.retained,
                            homie_qos(item.retained));
        if (item.source) item.source->queued_publish_done(item.payload, item.length, sent);
    }
}

bool PahoTransport::queue_publish(const char* topic, const char* payload, int length,
                                  bool retained, Property* source) {
    if (length < 0) length = 0;
    size_t topic_len = strlen(topic);
    bool ok = false;
    if (length > Property::VALUE_MAX) {
        homie_logf("PAHO: queued payload for '%s' is %d bytes, over %d; refused\n", topic,
                   length, Property::VALUE_MAX);
    } else if (topic_len > HOMIE_TOPIC_MAX) {
        homie_logf("PAHO: queued topic is %u chars, over %d; refused: %s\n",
                   (unsigned)topic_len, HOMIE_TOPIC_MAX, topic);
    } else {
        std::lock_guard<std::mutex> lock(_queue_lock);
        if (_queue_count < EBUS_POSIX_PUBLISH_QUEUE) {
            Queued& q = _queue[(_queue_head + _queue_count) % EBUS_POSIX_PUBLISH_QUEUE];
            memcpy(q.topic, topic, topic_len + 1);
            if (length > 0) memcpy(q.payload, payload, (size_t)length);
            q.length = length;
            q.retained = retained;
            q.source = source;
            _queue_count++;
            ok = true;
        }
    }
    if (!ok) {
        uint32_t n = ++_queue_dropped;
        if (length <= Property::VALUE_MAX && topic_len <= HOMIE_TOPIC_MAX) {
            homie_logf("PAHO: publish queue full, '%s' dropped (%u dropped)\n", topic,
                       (unsigned)n);
        }
        if (source) source->queued_publish_done(payload, length, false);
    }
    return ok;
}

void PahoTransport::loop() {
    if (!_client) return;
    if (!_stopped && !_link_ready.load()) {
        uint32_t now = homie_now_ms();
        if (_was_up) {
            _was_up = false;
            _next_attempt_ms = now;   // first reconnect at once, then every interval
            homie_logln("PAHO: link down; holding retained and QoS 1/2 publishes");
        }
        if (!_attempted || (int32_t)(now - _next_attempt_ms) >= 0) {
            _attempted = true;
            _next_attempt_ms = now + _config.reconnect_interval_ms;
            if (try_connect()) _was_up = true;
        }
    }
    drain_inbound();
    drain_queue();
}

void PahoTransport::disconnect(int timeout_ms) {
    if (!_client) return;
    drain_queue();
    _stopped = true;
    _link_ready.store(false);
    _hold.clear();
    if (MQTTClient_isConnected(CLIENT(_client))) MQTTClient_disconnect(CLIENT(_client), timeout_ms);
    homie_logln("PAHO: disconnected");
}
