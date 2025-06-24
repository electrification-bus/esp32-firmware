#include "homie/Node.h"
#include <homie/Property.h>
#include <homie/Device.h>
Node::Node() {
}

Node::~Node() {
}

void Node::addProperty(Property* property) {
    property->setNode(this); // Set the node for the property
    _properties[_num_properties++] = property;
    Serial.printf("node::addProperty: Property %s added to node %s\n", property->id(), this->id());
}

void Node::publish() {
    for(int i=0;i<_num_properties;i++) {
        _properties[i]->publish();
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
    this->_mqtt_client = client;
}