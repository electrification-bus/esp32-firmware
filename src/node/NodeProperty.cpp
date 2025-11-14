#include <Arduino.h>
#include <node/NodeProperty.h>
#include <mqtt_client.h>
#include <config.h>

void NodeProperty::publish() {
    mqtt_client.publish(_topic, _value);
}
void NodeProperty::setup(const char* node_id, const char* property_id) {
    set_homie_property_id(property_id);
    char _topic[128] = {0};
    sprintf(_topic, "%s%s/%s", theDevice->topic(), node_id, property_id);
    set_topic(_topic);

}
void NodeProperty::set(int value) {
    _intValue = value;
    snprintf(_value, sizeof(_value), "%d", value);
}   
void NodeProperty::set(float value) {
    _floatValue = value;
    snprintf(_value, sizeof(_value), "%.4f", value); //4 decimal precision
}
void NodeProperty::set(const char* value) {
    strcpy(_stringValue, value);
    strcpy(_value, value);
}
void NodeProperty::set(bool value) {
    _boolValue = value;
    strcpy(_value, value ? "true" : "false");
}
void NodeProperty::set(unsigned int value) {
    _unsignedValue = value;
    snprintf(_value, sizeof(_value), "%u", value);
}
void NodeProperty::set(double value) {
    _doubleValue = value;
    snprintf(_value, sizeof(_value), "%f", value);
}

void NodeProperty::set_homie_property_id(const char* id) {
    strcpy(_homie_property_id, id);
}
void NodeProperty::set_value_and_publish() {
    // Implementation for publishing the current value
}
void NodeProperty::set_value_and_publish(int value) {
    set(value);
    publish();
}
void NodeProperty::set_value_and_publish(float value) {
    set(value);
    publish();
}
void NodeProperty::set_value_and_publish(const char* value) {
    set(value);
    publish();
}
void NodeProperty::set_value_and_publish(bool value) {
    set(value);
    publish();
}
void NodeProperty::set_value_and_publish(unsigned int value) {
    set(value);
    publish();
}