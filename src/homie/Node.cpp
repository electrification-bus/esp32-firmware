#include "homie/Node.h"
#include <homie/Property.h>
#include <homie/Device.h>
Node::Node() {
    _num_properties = 0;
}

Node::~Node() {
}

void Node::addProperty(Property* property) {
    property->setNode(this); // Set the node for the property
    _properties[_num_properties++] = property;
}

void Node::publish(const char* topic) {
    String top(topic);
    top.concat(_id);
    top.concat("/");
    for(int i=0;i<_num_properties;i++) {
        Serial.printf("Node: Publishing to topic %d: '%s'\n", i, top.c_str());
        _properties[i]->publish(top.c_str() );
    }
}

void Node::setName(const char* name) {
    strncpy(_name, name, sizeof(_name) - 1);
    _name[sizeof(_name) - 1] = '\0'; // Ensure null termination
}

void Node::setId(const char* id) {
    strncpy(_id, id, sizeof(_id) - 1);
    _id[sizeof(_id) - 1] = '\0'; // Ensure null termination
}

const char* Node::name() {
    return _name;
}  

const char* Node::id() {
    return _id;
}

void Node::setDevice(Device* _device) {
   this->_device = _device;
}

Device* Node::device() {
   return _device;
}

void Node::setMQTTClient(PubSubClient* client) {
    _mqtt_client = client;
    for(int i=0; i < _num_properties; i++) {
        _properties[i]->setMQTTClient(client); // Set the MQTT client for each property
    }
}