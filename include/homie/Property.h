#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <platform/mqtt_client.h>
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
    bool publish_value();
    void mqtt_settable_callback(const char* topic, const char* payload);
    void device_new_value_callback(const char* sensor_value);
    void subscribe();
    void from_dict(JsonObject* props_obj);
    const char* topic();
    void publish();
    Node* getParentNode() const { return _parent_node; }
    void serialize(JsonDocument& json);
    void serializeInto(JsonObject& obj);
    bool is_dirty() const { return _dirty_settable; }
    void clear_dirty() { _dirty_settable = false; }
private:
    bool _dirty_settable = false;
    bool _has_value = false;   // C3: false until setValue() — guards phantom retained-empty topics
    char _id[32] = {0};
    char _name[32] = {0};
    char _value[256] = {0};
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
    char _stringValue[256];
    float _floatValue;
    uint64_t _unsignedValue;
    int _intValue;
};