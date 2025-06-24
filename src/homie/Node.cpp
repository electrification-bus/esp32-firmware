#include "homie/Node.h"
#include "homie/Property.h"

Node::Node() {
}

Node::~Node() {
}

void Node::addProperty(Property* property) {
    Serial.printf("node::addProperty: Adding property %s to node %s\n", property->id(), this->id());
}

void Node::setName(const char* name) {
    Serial.printf("Node::set_name: %s\n",name);
    strncpy(_name, name, sizeof(_name) - 1);
    _name[sizeof(_name) - 1] = '\0'; // Ensure null termination
}
void Node::setId(const char* id) {
    Serial.printf("Node::set_id: %s\n", id);
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

void Node::setMQTTClient(PubSubClient* client) {
    this->_mqtt_client = client;
}