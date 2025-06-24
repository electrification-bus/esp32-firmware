#pragma once

#include <Arduino.h>
#include <mqtt_client.h>
#include <SimpleMap.h>
class Node;
class Device;

class Property {
public:
    Property();
    ~Property(){};

    //static Property* from_dict(const void* property_dict);

    void setId(const char* id);
    const char* id() const;
    void setNode(Node* node);
    Node* node() const;
    void setDevice(Device* device);
    const char* name() const;
    void setName(const char* name);
    const char* device() const;
    void setValue(const char* value);
    const char* value() const;
    const char* setFormat() const;
    const char* format() const;
    const char* coerced_value() const;

    void setDatatype(const char*);
    const char* datatype() const;
    const char* unit() const;
    PubSubClient* mqttClient() const;
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
    void publish();

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
    PubSubClient* _mqtt_client;
    void* _async_loop = nullptr;
};