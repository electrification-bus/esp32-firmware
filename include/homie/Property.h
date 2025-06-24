#pragma once

#include <Arduino.h>
#include <SimpleMap.h>

// Forward declarations for types used as pointers
class Node;
class Device;
class MqttClient;

class Property {
public:
    Property(
        const char* id,
        const char* name,
        const char* datatype);
        /*,
        const char* format,
        bool settable,
        void* callback,
        bool retained,
        const char* unit = nullptr,
        int round_to = 0,
        bool supports_target = false,
        Node* node = nullptr,
        Device* device = nullptr,
        void* async_loop = nullptr
    );*/

    //static Property* from_dict(const void* property_dict);

    void set_node(Node* node);
    Node* get_node() const;
    const char* id() const;
    const char* nodeId() const;
    const char* deviceId() const;
    void set_device(Device* device);
    bool set_value(const char* value);

    const char* value() const;
    const char* format() const;
    const char* coerced_value() const;

    const char* datatype() const;
    const char* unit() const;
    MqttClient* mqtt_client() const;
    void start_mqtt_client();
    bool is_settable() const;
    bool is_retained() const;
    bool is_json_datatype() const;
    void set_callback() const;
    void publish_target_value(const char* payload);
    bool publish_value();
    void description(SimpleMap<const char*, const char*>& desc) const;
    void _settable_callback(const char* topic, const char* payload);
    void set_subscribe();

private:
    char _id[64];
    char _name[256];
    char _value[256];
    char _datatype[8];
    char _format[8];
    bool _settable;
    void* _callback;
    bool _retained;
    char _unit[8];
    int _round_to = 0;
    bool _supports_target = false;
    Node* _node = nullptr;
    Device* _device = nullptr;
    void* _async_loop = nullptr;
};