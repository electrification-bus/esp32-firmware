#include <ArduinoYaml.h>
#include <homie/homie.h>
#include <homie/Node.h>
#include <homie/Property.h>
#include <homie/Device.h>
Node::Node() : _mqtt_client(nullptr), _device(nullptr) {
    _num_properties = 0;
}

Node::~Node() {
}

JsonDocument Node::serialize() {
    JsonDocument json;
    json[HOMIE_NAME] = _name;
    json[HOMIE_TYPE] = _type;

    //properties
    JsonDocument props;
    JsonDocument obj;
    for(int i=0;i<_num_properties;i++) {
        _properties[i]->serialize(obj);
        props[_properties[i]->id()] = obj;
    }
    json[HOMIE_PROPERTIES] = props;
    return json;
}

void Node::addProperty(Property* property) {
    Serial.printf("Node '%s': Adding property: '%s'\n", _id, property->id());
    //instantiate NodeProperty and add to array
    property->setNode(this); // Set the parent node for the property
    _properties[_num_properties++] = property;
}

void Node::publish() {
    for(int i=0;i<_num_properties;i++) {
        _properties[i]->publish();
    }
}

void Node::setName(const char* name) {
    strcpy(_name, name);
}

const char* Node::name() {
    return _name;
}  

void Node::setType(const char* type) {
    strcpy(_type, type);
}

const char* Node::type() {
    return _type;
}

void Node::setId(const char* id) {
    strcpy(_id, id);
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

void Node::setMQTTClient(MQTTClient* client) {
    _mqtt_client = client;
    for(int i=0; i < _num_properties; i++) {
        _properties[i]->setMQTTClient(client); // Set the MQTT client for each property
    }
}

void Node::mqttConnected() {
    for(int i=0;i<_num_properties;i++) {
        //re-subscribe to set topic if property is settable
        if (_properties[i]->settable()) {
            _properties[i]->subscribe();
        }
    }
}

Property* Node::getProperty(const char* id) {
    for(int i=0;i<_num_properties;i++) {
        if (strcmp(_properties[i]->id(), id) == 0) {
            return _properties[i];
        }
    }
    Serial.printf("Node: '%s' - Property '%s' not found\n", _id, id);
    return nullptr; // Property not found
}

void Node::settable_callback(Property* property) {
    // Notify the device about the property change
    Serial.printf("Node: '%s' send/set property change to/on Device '%s' change\n", _id, property->id());
}