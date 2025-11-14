#include <Arduino.h>
#include <node/NodeProperty.h>
#include <mqtt_client.h>

void NodeProperty::publish() {
    mqtt_client.publish(_topic, _value);
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