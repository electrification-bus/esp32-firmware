#include "homie/Property.h"
#include <homie/Node.h>
#include <homie/Device.h>
Property::Property(){};

void Property::setNode(Node* node) {
    strncpy(_id, node->id(), sizeof(_id) - 1);
    _id[sizeof(_id) - 1] = '\0'; // Ensure null termination
}

Node* Property::node() const {
    return nullptr;
}

void Property::setId(const char* id) {
    strncpy(_id, id, sizeof(_id) - 1);
    _id[sizeof(_id) - 1] = '\0'; // Ensure null termination
}

const char* Property::id() const {
    return _id;
}

void Property::setName(const char* name) {
    strncpy(_name, name, sizeof(_name) - 1);
    _name[sizeof(_name) - 1] = '\0'; // Ensure null termination}
}

const char* Property::name() const {
    return _name;
}

void Property::setDevice(Device* device) {
    _device = device;
}

void Property::setValue(const char* value) {

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


bool Property::is_settable() const {
    return false;
}


bool Property::is_retained() const {
    return false;
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