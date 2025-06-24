#include "homie/Property.h"
#include <homie/Node.h>
#include <homie/Device.h>
Property::Property(){};

void Property::from_dict(JsonObject* props_obj) {
    setId((*props_obj)["id"].as<const char*>());
    setName((*props_obj)["name"].as<const char*>()); 
    setDatatype((*props_obj)["datatype"].as<const char*>());
    //setUnit((*props_obj)["unit"].as<const char*>());
    //setValue((*props_obj)["value"].as<const char*>());
    //setFormat((*props_obj)["format"].as<const char*>());
    //setDatatype((*props_obj)["settable"].as<const char*>());
    //setRetained((*props_obj)["retained"].as<bool>());
    /*

    char _unit[8];
    int _round_to = 0;
    bool _supports_target = false;*/
}

void Property::setNode(Node* node) {
    strncpy(_id, node->id(), sizeof(_id) - 1);
    _id[sizeof(_id) - 1] = '\0'; // Ensure null termination
}

Node* Property::node() {
    return _node;
}

void Property::setId(const char* id) {
    strncpy(_id, id, sizeof(_id) - 1);
    _id[sizeof(_id) - 1] = '\0';
}

const char* Property::id() const {
    return _id;
}

void Property::setUnit(const char* unit) {
    strncpy(_unit, unit, sizeof(_unit) - 1);
    _unit[sizeof(_unit) - 1] = '\0';
}

const char* Property::unit() {
    return _unit;
}
void Property::setName(const char* name) {
    strncpy(_name, name, sizeof(_name) - 1);
    _name[sizeof(_name) - 1] = '\0';
}

const char* Property::name() const {
    return _name;
}

void Property::setDevice(Device* device) {
    _device = device;
}

void Property::setValue(const char* value) {

}

void Property::setFormat(const char* fmt) {
    strncpy(_format, fmt, sizeof(_format) - 1);
    _format[sizeof(_format) - 1] = '\0';
}

const char* Property::coerced_value() const {
    return "";
}

void Property::setDatatype(const char* dt)  {
    strncpy(_datatype, dt, sizeof(_datatype) - 1);
    _datatype[sizeof(_datatype) - 1] = '\0'; // Ensure null termination}
}

const char* Property::datatype() const {
    return "";
}

PubSubClient* Property::mqttClient() const {
    return _mqtt_client;
}

void Property::start_mqtt_client() {}


bool Property::settable() {
    return false;
}


void Property::setSettable(bool s) {
   _settable = s;;
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

void Property::set_subscribe() {}

void Property::publish() {
    //boolean PubSubClient::publish(const char* topic, const uint8_t* payload, unsigned int plength, boolean retained) {
    _mqtt_client->publish(_name, _value);
}