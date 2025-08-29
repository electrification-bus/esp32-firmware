#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mqtt_client.h>
class Node;

class Property {
public:
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
    const char* value();

    const char* coerced_value() const;

    void setDatatype(const char* dt);
    const char* datatype() const;
    void setUnit(const char* unit);
    const char* unit();
    void setFormat(const char* fmt);
    const char* format();
    void setMQTTClient(PubSubClient* client);
    PubSubClient* mqttClient() const;
    void start_mqtt_client();
    void setSettable(bool settable);
    bool settable();
    void setRetained(bool r);
    bool retained();
    bool is_json_datatype() const;
    void set_callback() const;
    void publish_target_value(const char* payload);
    bool publish_value();
    void mqtt_settable_callback(const char* topic, const char* payload);
    void device_new_value_callback(const char* sensor_value);
    void subscribe();
    void from_dict(JsonObject* props_obj);
    const char* topic();
    void publish();
    void serialize(JsonDocument& json);
private:
    char _id[32] = {0};
    char _name[32] = {0};
    char _value[32] = {0};
    char _datatype[8] = {0};
    char _topic[64] = {0};
    char _format[16] = {0};
    bool _settable;
    void* _callback;
    bool _retained = true;
    char _unit[8] = {0};
    int _round_to = 0;
    bool _supports_target = false;
    Node* _parent_node = nullptr;
    PubSubClient* _mqtt_client;
    void* _async_loop = nullptr;

    bool _boolValue;
    char _stringValue[32];
    float _floatValue;
    uint64_t _unsignedValue;
    int _intValue;
};