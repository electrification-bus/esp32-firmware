#pragma once

#include <Arduino.h>
#include <platform/mqtt_client.h>
class Node;

class Property {
public:
    // Longest value, in chars, a property holds and publishes. Matches MQTT_PAYLOAD_MAX in
    // mqtt_client.cpp, so the property model and the publish queue carry the same payloads.
    static const int VALUE_MAX = 256;

    Property();
    Property(Node* parent_node);
    ~Property(){};

    void setId(const char* id);
    const char* id() const;
    void setNode(Node* node);
    Node* node();
    void setName(const char* name);
    const char* name() const;

    void setValue(int value);
    void setValue(float value);
    void setValue(const char* value);
    void setValue(bool value);
    void setValue(unsigned int value);
    const char* value() { return _value; };
    float getFloatValue() { return _floatValue; };
    int getIntValue() { return (float)_intValue; };
    bool getBoolValue() { return _boolValue; };
    // Enum value is the selected string (one of the format options); color value is the
    // raw "type,c1,c2[,c3]" payload — use homie_parse_color() for the components (C1).
    const char* getEnumValue() { return _value; };
    const char* getColorValue() { return _value; };

    const char* coerced_value() const;

    void setDatatype(const char* dt);
    const char* datatype() const;
    void setUnit(const char* unit);
    const char* unit();
    void setFormat(const char* fmt);
    const char* format();
    //void setMQTTClient(PubSubClient* client);
    void setMQTTClient(MQTTClient* client);
    //PubSubClient* mqttClient() const;
    MQTTClient* mqttClient() const;
    void start_mqtt_client();
    void setSettable(bool settable);
    bool settable();
    void setSupportsTarget(bool t);   // device publishes $target on accepted /set (C5)
    bool supportsTarget();
    void setRetained(bool r);
    bool retained();
    bool is_json_datatype() const;
    void set_callback() const;
    void publish_target_value(const char* payload);

    // Publish this property's value.
    //
    // publish_value()  — direct, for the main task (tree/$description/set echo).
    // publish_queued() — via mqtt_queue, safe from ANY task. Drivers reach this through
    //                    NodeProperty::publish(); it is the path a BLE or Modbus task uses.
    //
    // Both are gated on change: a RETAINED property whose payload is byte-identical to the
    // one it last published is not republished, because the broker's retained store already
    // holds exactly that and every subscriber already has it. `force` bypasses the gate for
    // a whole-tree republish after reconnect.
    bool publish_value(bool force = false);
    bool publish_queued(bool force = false);

    void clearValue();   // mark unavailable — retract retained topic, no sentinel
    // The /set path, in the order dispatch_settable() runs it: store_set_payload()
    // validates and stores; the driver, if any, accepts or refuses; publish_set() reports
    // an accepted value, and restore_value() undoes a refused one.
    bool store_set_payload(const char* payload);
    void publish_set(const char* payload, bool value_queued);
    // Count of publishes publish_queued() has handed off. dispatch_settable() compares it
    // across the driver call to tell whether the driver published the value itself.
    uint32_t queued_count() const { return __atomic_load_n(&_queued_count, __ATOMIC_RELAXED); }

    // A plain copy of the value state (text and typed fields), with no lock: dispatch
    // takes and restores it on the main task, so a settable property must be stored only
    // from the main task, or a restore could overwrite a value another task just stored.
    // A restore does not recall a publish the driver already queued, so a driver that
    // refuses must not have set or published the property itself.
    struct ValueSnapshot {
        char value[VALUE_MAX + 1];
        bool has_value;
        bool bool_value;
        float float_value;
        uint64_t unsigned_value;
        int int_value;
    };
    void save_value(ValueSnapshot* s) const;
    void restore_value(const ValueSnapshot* s);
    void device_new_value_callback(const char* sensor_value);
    void subscribe();
    const char* topic();
    void publish();
    Node* getParentNode() const { return _parent_node; }
    // JSON (de)serialisation lives in homie/homie_json.h as free functions, so
    // ArduinoJson stays out of this header — see property_serialize_into().
    bool is_dirty() const { return _dirty_settable; }
    void clear_dirty() { _dirty_settable = false; }
    // Payload last actually put on the wire, for the change gate. Exposed for tests and
    // for anything that needs to know what the broker currently holds.
    const char* last_published() const { return _last_pub_len >= 0 ? _last_pub : nullptr; }
    // Called by the publish queue exactly once for every message publish_queued()
    // handed it, when the message is sent (sent=true) or dropped. Not for drivers.
    void queued_publish_done(const char* payload, int len, bool sent);

private:
    // True when this payload should reach the wire. Four carve-outs, all deliberate:
    //   - a NON-RETAINED (event) property is never gated: the broker stores nothing for it,
    //     so an identical consecutive payload is a second real event, not a redundant
    //     write, and suppressing it would lose the event.
    //   - `force` is never gated (whole-tree republish after a reconnect).
    //   - a payload longer than the memo is never gated — see PROPERTY_MEMO_MAX.
    //   - nothing is gated while a queued publish is in flight (see _in_flight).
    bool gate_allows(const char* payload, int len, bool force);
    void note_published(const char* payload, int len);

    // The memo is deliberately smaller than _value. Comparing exactly means never
    // WRONGLY suppressing a publish; the cost is that values longer than this are always
    // published. Sizing it to _value would add 256 bytes to every property on the device
    // for a case (long JSON payloads) that is rare and republishes cheaply anyway.
    static const int PROPERTY_MEMO_MAX = 64;
    char _last_pub[PROPERTY_MEMO_MAX] = {0};
    int  _last_pub_len = -1;          // -1 = nothing published on this topic yet
    // Messages publish_queued() has enqueued that the queue has not yet sent or dropped.
    // While any are in flight the gate never suppresses: the memo describes the last one
    // SENT, and a still-queued different value would land after it.
    volatile uint32_t _in_flight = 0;
    uint32_t _queued_count = 0;       // see queued_count()

    bool _dirty_settable = false;
    bool _has_value = false;   // C3: false until setValue() — guards phantom retained-empty topics
    char _id[32] = {0};
    char _name[32] = {0};
    char _value[VALUE_MAX + 1] = {0};
    char _datatype[16] = {0};
    char _topic[64] = {0};
    char _format[64] = {0};   // enum/color formats (comma lists) need room (C1)
    bool _settable;
    void* _callback;
    bool _retained = true;
    char _unit[8] = {0};
    int _round_to = 0;
    bool _supports_target = false;
    Node* _parent_node = nullptr;
    //PubSubClient* _mqtt_client;
    MQTTClient* _mqtt_client;
    void* _async_loop = nullptr;

    bool _boolValue;
    float _floatValue;
    uint64_t _unsignedValue;
    int _intValue;
};