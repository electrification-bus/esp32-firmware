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
        const char* id = "",
        const char* value = "",
        const char* name = "",
        const char* datatype = "",
        const char* format = "",
        bool settable = false,
        void (*set_callback)(const char*) = nullptr,
        bool retained = true,
        const char* unit = "",
        int round_to = -1,
        bool supports_target = false,
        Node* node = nullptr,
        Device* device = nullptr,
        void* async_loop = nullptr,
        void* from_dict = nullptr
    );

    static Property from_dict(const void* property_dict);

    void set_node(Node* node);
    Node* node() const;
    Node* get_node() const;
    const char* get_node_id() const;
    const char* get_device_id() const;
    void set_device(Device* device);
    bool set_value(const char* value);
    bool set(const char* value);
    int round() const;
    const char* value() const;
    const char* format() const;
    const char* get() const;
    const char* coerced_value() const;
    const char* get_coerced_value() const;
    const char* id() const;
    const char* get_id() const;
    const char* datatype() const;
    const char* get_datatype() const;
    MqttClient* get_mqtt_client() const;
    void start_mqtt_client();
    bool settable() const;
    bool is_settable() const;
    bool retained() const;
    bool is_retained() const;
    bool is_json_datatype() const;
    void (*get_set_callback() const)(const char*);
    bool supports_target() const;
    void publish_target_value(const char* payload);
    bool publish_value();
    void description(SimpleMap<const char*, const char*>& desc) const;
    void _settable_callback(const char* topic, const char* payload);
    void set_subscribe();

private:
    // Internal storage and members would go here
};