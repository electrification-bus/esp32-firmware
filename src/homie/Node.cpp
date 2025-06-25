#include "homie/Node.h"
#include <homie/Property.h>
#include <homie/Device.h>
Node::Node() {
    _num_properties = 0;
}

Node::~Node() {
}

void Node::addProperty(Property* property) {
    _properties[_num_properties] = property;
    _num_properties++;
}

void Node::publish() {
    for(int i=0;i<_num_properties;i++) {
        _properties[i]->publish();
    }
}

void Node::setName(const char* name) {
    strcpy(_name, name);
}

void Node::setId(const char* id) {
    strcpy(_id, id);
}

const char* Node::name() {
    return _name;
}  

const char* Node::id() {
    return _id;
}

void Node::setDevice(Device* _device) {
   _device = _device;
}

Device* Node::device() {
   return _device;
}

void Node::setTopic(const char* top) {
    sprintf(_topic, "%s%s", top, _id);
}

const char* Node::topic() {
    return _topic;
}

void Node::setMQTTClient(PubSubClient* client) {
    _mqtt_client = client;
    for(int i=0; i < _num_properties; i++) {
        _properties[i]->setMQTTClient(client); // Set the MQTT client for each property
    }
}