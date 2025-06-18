#include "Property.h"

Property::Property(
    const char* id,
    const char* value,
    const char* name,
    const char* datatype,
    const char* format,
    bool settable,
    void (*set_callback)(const char*),
    bool retained,
    const char* unit,
    int round_to,
    bool supports_target,
    Node* node,
    Device* device,
    void* async_loop,
    void* from_dict
) {}

Property Property::from_dict(const void* property_dict) {
    return Property();
}

void Property::set_node(Node* node) {}

Node* Property::node() const {
    return nullptr;
}

Node* Property::get_node() const {
    return nullptr;
}

const char* Property::get_node_id() const {
    return "";
}

const char* Property::get_device_id() const {
    return "";
}

void Property::set_device(Device* device) {}

bool Property::set_value(const char* value) {
    return false;
}

bool Property::set(const char* value) {
    return false;
}

int Property::round() const {
    return 0;
}

const char* Property::value() const {
    return "";
}

const char* Property::format() const {
    return "";
}

const char* Property::get() const {
    return "";
}

const char* Property::coerced_value() const {
    return "";
}

const char* Property::get_coerced_value() const {
    return "";
}

const char* Property::id() const {
    return "";
}

const char* Property::get_id() const {
    return "";
}

const char* Property::datatype() const {
    return "";
}

const char* Property::get_datatype() const {
    return "";
}

MqttClient* Property::get_mqtt_client() const {
    return nullptr;
}

void Property::start_mqtt_client() {}

bool Property::settable() const {
    return false;
}

bool Property::is_settable() const {
    return false;
}

bool Property::retained() const {
    return false;
}

bool Property::is_retained() const {
    return false;
}

bool Property::is_json_datatype() const {
    return false;
}

void (*Property::get_set_callback() const)(const char*) {
    return nullptr;
}

bool Property::supports_target() const {
    return false;
}

void Property::publish_target_value(const char* payload) {}

bool Property::publish_value() {
    return false;
}

void Property::description(SimpleMap<const char*, const char*>& desc) const {}

void Property::_settable_callback(const char* topic, const char* payload) {}

void Property::set_subscribe() {}