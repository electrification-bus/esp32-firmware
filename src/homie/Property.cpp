#include <config.h>
#include "homie/Property.h"
#include <homie/Node.h>
#include <homie/Device.h>
Property::Property() {
    _node = nullptr;
};

void Property::from_dict(JsonObject* props_obj) {
    if ((*props_obj)["id"].is<const char*>()) {
        setId((*props_obj)["id"].as<const char*>());
    }
    if ((*props_obj)["name"].is<const char*>()) {
        setName((*props_obj)["name"].as<const char*>()); 

    }
    if ((*props_obj)["datatype"].is<const char*>()) {
        setDatatype((*props_obj)["datatype"].as<const char*>());
    }
    if ((*props_obj)["value"].is<bool>()) {
        setValue((*props_obj)["value"].as<bool>());
    }
    if ((*props_obj)["value"].is<float>()) {
        setValue((*props_obj)["value"].as<float>());
    }
    if ((*props_obj)["value"].is<const char*>()) {
        setValue((*props_obj)["value"].as<const char*>());
    }
    
    //setFormat((*props_obj)["format"].as<const char*>());
    setSettable((*props_obj)["settable"].as<bool>());
}

void Property::setNode(Node* node) {
    _node = node;
}

Node* Property::node() {
    return _node;
}

void Property::setId(const char* id) {
    strcpy(_id, id);
    //SET-ID SIDE-EFFECT - construct the topic
    sprintf(_topic, "%s/%s", _node->topic(), _id);
}

const char* Property::id() const {
    return _id;
}

void Property::setUnit(const char* unit) {
    strcpy(_unit, unit);
}

const char* Property::unit() {
    return _unit;
}

void Property::setName(const char* name) {
    strcpy(_name, name);
}

const char* Property::name() const {
    return _name;
}

void Property::setValue(int value) {
    _intValue = value;
    sprintf(_value, "%d", value);
}
void Property::setValue(float value) {
    _floatValue = value;
    snprintf(_value, sizeof(_value), "%f", value);
}
void Property::setValue(const char* value) {
    strcpy(_stringValue, value);
    strcpy(_value, value);
}
void Property::setValue(bool value) {
    _boolValue = value;
    strcpy(_value, value ? "true" : "false");
}
void Property::setValue(unsigned int value) {
    _unsignedValue = value;
    sprintf(_value, "%u", value);
}

void Property::setFormat(const char* fmt) {
    strcpy(_format, fmt);
}

const char* Property::value() {
    return _value;
}
const char* Property::coerced_value() const {
    return "";
}

void Property::setDatatype(const char* dt)  {
    strcpy(_datatype, dt);
}

const char* Property::datatype() const {
    return "";
}

PubSubClient* Property::mqttClient() const {
    return _mqtt_client;
}

void Property::start_mqtt_client() {}


bool Property::settable() {
    return _settable;
}

void Property::setSettable(bool s) {
   _settable = s;
}

void Property::setRetained(bool r) {
    _retained = r;
}

bool Property::retained() {
    return _retained;
}

bool Property::is_json_datatype() const {
    return false;
}
void Property::set_callback() const{
}

void Property::publish_target_value(const char* payload) {}

bool Property::publish_value() {
    return false;
}

void Property::description(SimpleMap<const char*, const char*>& desc) const {}

void Property::_settable_callback(const char* topic, const char* payload) {}

void Property::setSubscribe() {}

const char* Property::topic() {
    return _topic;
}

void Property::setMQTTClient(PubSubClient* client) {
    _mqtt_client = client;
}

void Property::publish() {
    _mqtt_client->publish(topic(), _value); //TODO retained flag
}