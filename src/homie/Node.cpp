#include <homie/homie.h>
#include <homie/Node.h>
#include <homie/Property.h>
#include <homie/Device.h>
Node::Node() {
    _num_properties = 0;
}

Node::~Node() {
}

void Node::serialize(String& serialized) {
    JsonDocument doc;
    doc[HOMIE_ID] = _id;
    doc[HOMIE_NAME] = _name;
    doc[HOMIE_TYPE] = _type;

    //properties
    JsonDocument props;
    String prop_serial;
    for(int i=0;i<_num_properties;i++) {
      _properties[i]->serialize(prop_serial);
      props[_properties[i]->id()] = prop_serial;
    }
    doc[HOMIE_PROPERTIES] = props;
    serializeJson(doc, serialized);
}

void Node::addProperty(Property* property) {
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

void Node::setMQTTClient(PubSubClient* client) {
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