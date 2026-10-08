#pragma once
// A HomieTransport for host tests: records every publish, queued publish and subscribe in
// order, so a test can assert exactly what would have gone on the wire.
//
// queue_publish() only records. The real queue reports back later, when its owner task
// sends or drops the message; a test plays that part by calling
// Property::queued_publish_done() itself (see FakeTransport::Record::source).
#include <homie/homie_transport.h>
#include <stdio.h>
#include <string.h>

class FakeTransport final : public HomieTransport {
 public:
    static const int MAX_RECORDS = 64;
    static const int TOPIC_MAX = 160;
    static const int PAYLOAD_MAX = 64;   // longer payloads ($description) are cut short

    enum Kind { PUBLISH, QUEUED, SUBSCRIBE };

    struct Record {
        Kind kind;
        char topic[TOPIC_MAX];
        char payload[PAYLOAD_MAX];
        int length;          // bytes the caller asked to publish (0 for a subscribe)
        bool retained;
        int qos;
        Property* source;    // queue_publish() only
    };

    bool is_connected = true;
    bool publish_result = true;
    bool queue_result = true;
    bool subscribe_result = true;

    using HomieTransport::publish;
    bool publish(const char* topic, const char* payload, int length, bool retained,
                 int qos) override {
        record(PUBLISH, topic, payload, length, retained, qos, nullptr);
        return is_connected && publish_result;
    }
    bool queue_publish(const char* topic, const char* payload, int length, bool retained,
                       Property* source) override {
        record(QUEUED, topic, payload, length, retained, homie_qos(retained), source);
        return queue_result;
    }
    bool subscribe(const char* topic, int qos) override {
        record(SUBSCRIBE, topic, "", 0, false, qos, nullptr);
        return is_connected && subscribe_result;
    }
    bool connected() override { return is_connected; }
    int last_error() override { return -3; }

    void clear() { _count = 0; }
    int count() const { return _count; }
    const Record& at(int i) const { return _records[i]; }
    // How many records are of `kind`, and the n-th of them (0-based), or null.
    int count_of(Kind kind) const {
        int n = 0;
        for (int i = 0; i < _count && i < MAX_RECORDS; i++) n += _records[i].kind == kind;
        return n;
    }
    const Record* nth(Kind kind, int n) const {
        for (int i = 0; i < _count && i < MAX_RECORDS; i++) {
            if (_records[i].kind == kind && n-- == 0) return &_records[i];
        }
        return nullptr;
    }

 private:
    void record(Kind kind, const char* topic, const char* payload, int length, bool retained,
                int qos, Property* source) {
        if (_count < MAX_RECORDS) {
            Record& r = _records[_count];
            r.kind = kind;
            snprintf(r.topic, TOPIC_MAX, "%s", topic);
            int n = length < PAYLOAD_MAX - 1 ? length : PAYLOAD_MAX - 1;
            if (n < 0) n = 0;
            memcpy(r.payload, payload, (size_t)n);
            r.payload[n] = '\0';
            r.length = length;
            r.retained = retained;
            r.qos = qos;
            r.source = source;
        }
        _count++;
    }

    Record _records[MAX_RECORDS];
    int _count = 0;
};
