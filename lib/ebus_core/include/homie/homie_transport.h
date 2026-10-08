#pragma once
// The MQTT client as the Homie layer sees it. Device, Node, Property, the settable table
// and the controller publish and subscribe only through this interface; a port
// implements it over its own MQTT client (src/platform/mqtt_client.h on the ESP32).
#include <stdint.h>
#include <string.h>

class Property;

// Largest MQTT payload the firmware accepts or publishes: a $description is serialized
// into a buffer of this size, and the controller inbox must hold one. Define it in the
// build flags of the core and the port alike (not a source-only flag), so both agree.
#ifndef MAX_DATA_LEN
#define MAX_DATA_LEN 8192
#endif

class HomieTransport {
 public:
    // Publish exactly `length` bytes (a single 0x00 byte is the Homie empty-string value;
    // a zero-length retained payload deletes the topic). Main task only. Returns false
    // when the client is not connected or the publish failed.
    virtual bool publish(const char* topic, const char* payload, int length, bool retained,
                         int qos) = 0;
    // NUL-terminated payload.
    bool publish(const char* topic, const char* payload, bool retained, int qos) {
        return publish(topic, payload, (int)strlen(payload), retained, qos);
    }

    // Publish from ANY task: the port hands the message to the task that owns the client.
    // QoS follows homie_qos(retained). When `source` is not null the port calls
    // source->queued_publish_done(payload, length, sent) exactly once, when the message
    // is sent or dropped, including when this call itself fails.
    virtual bool queue_publish(const char* topic, const char* payload, int length,
                               bool retained, Property* source) = 0;

    // Subscribe one topic filter. Main task only, and never from inside the receive
    // callback: a client that waits for the SUBACK delivers other messages meanwhile.
    virtual bool subscribe(const char* topic, int qos) = 0;

    virtual bool connected() = 0;

    // The client's error code for the last failed operation, for logs only.
    virtual int last_error() = 0;

 protected:
    ~HomieTransport() {}
};

// QoS for RETAINED Homie publications (property values + $state/$description).
// Homie 5 recommends QoS 2 (SHOULD), so it defaults to 2; the port may change it (the
// ESP32 firmware reads the `mqtt_qos` config key). Non-retained publications and /set are
// fixed at QoS 0 per the spec (a hard requirement), so homie_qos() returns 0 for them
// regardless. (C4)
extern uint8_t mqtt_qos;
inline int homie_qos(bool retained) { return retained ? (int)mqtt_qos : 0; }
