#pragma once
// Publishes made while the broker link is down, kept for the next connect. The rules are
// ebus-mqtt-client's (README "Publishing before the connection is up"):
//
//   - retained, any QoS: held, newest value per topic. A newer value for a held topic
//     replaces the older one and moves to the back of the flush order.
//   - not retained, QoS 1 or 2: held in order, one entry per publish.
//   - not retained, QoS 0: dropped.
//
// Bounded: when full, the oldest entry is evicted. Fixed storage, no allocation. Not
// thread-safe: the thread that owns the MQTT client is the only one that touches it.
#include <homie/homie_limits.h>
#include <stdint.h>

#ifndef EBUS_POSIX_HOLD_ENTRIES
#define EBUS_POSIX_HOLD_ENTRIES 64
#endif
// Longest payload held. Property values are at most Property::VALUE_MAX (256); a longer
// payload (a $description) published while the link is down is dropped and logged.
#ifndef EBUS_POSIX_HOLD_PAYLOAD_MAX
#define EBUS_POSIX_HOLD_PAYLOAD_MAX 1024
#endif

class PublishHold {
 public:
    enum Result { HELD, REPLACED, DROPPED_QOS0, DROPPED_TOO_LARGE };

    // Sends one held publish on a live link. Returns true when the entry is done with
    // (sent, or refused for good), false to keep it held and stop the flush.
    typedef bool (*send_fn)(void* ctx, const char* topic, const char* payload, int length,
                            bool retained, int qos);

    Result hold(const char* topic, const char* payload, int length, bool retained, int qos);

    // Send every held publish, oldest first, removing each one sent. Stops at the first
    // that fails and keeps it and everything after it. Returns the number sent.
    int flush(send_fn send, void* ctx);

    void clear();
    int count() const { return _count; }
    // Entries evicted because the hold was full, since construction.
    uint32_t evicted() const { return _evicted; }

 private:
    struct Entry {
        bool used = false;
        bool retained = false;
        int qos = 0;
        int length = 0;
        uint32_t seq = 0;
        char topic[HOMIE_TOPIC_MAX + 1] = {0};
        char payload[EBUS_POSIX_HOLD_PAYLOAD_MAX] = {0};
    };

    int oldest() const;

    Entry _entries[EBUS_POSIX_HOLD_ENTRIES];
    int _count = 0;
    uint32_t _next_seq = 0;
    uint32_t _evicted = 0;
};
