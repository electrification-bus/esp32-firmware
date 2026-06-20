#include <ArduinoYaml.h>
#include <homie/homie.h>
#include <homie/homie_id.h>
#include <homie/Node.h>
#include <homie/Property.h>
#include <homie/Device.h>
Node::Node() : _mqtt_client(nullptr), _device(nullptr) {
    _num_properties = 0;
}

Node::~Node() {
}

void Node::serializeInto(JsonObject& obj) {
    obj[HOMIE_NAME] = _name;
    obj[HOMIE_TYPE] = _type;
    JsonObject props = obj[HOMIE_PROPERTIES].to<JsonObject>();
    for (int i = 0; i < _num_properties; i++) {
        JsonObject prop_obj = props[_properties[i]->id()].to<JsonObject>();
        _properties[i]->serializeInto(prop_obj);
    }
}

JsonDocument Node::serialize() {
    JsonDocument json;
    JsonObject obj = json.to<JsonObject>();
    serializeInto(obj);
    return json;
}

void Node::addProperty(Property* property) {
    if (_num_properties >= MAX_PROPERTIES) {
        Serial.printf("Node '%s': ERROR property limit (%d) reached — '%s' NOT added\n",
                      _id, MAX_PROPERTIES, property->id());
        return;
    }
    Serial.printf("Node '%s': Adding property: '%s'\n", _id, property->id());
    property->setNode(this);
    if (_mqtt_client) property->setMQTTClient(_mqtt_client);
    _properties[_num_properties++] = property;
}

void Node::addProperty(Property* property, const char* id, const char* name, const char* datatype, const char* unit, bool settable, bool retained, const char* format) {
    property->setNode(this);  // must precede setId() — setId() dereferences _parent_node->topic()
    // Property-id is a topic level: coerce to Homie-legal (a-z 0-9 -) on the publisher
    // side, matching NodeProperty::setup() so the value lookup stays consistent (rrj.2).
    // (This is the explicit-id publisher path; the controller's from_dict path keeps
    // exact wire ids.)
    char prop_id[32] = {0};
    sanitize_homie_id(id, prop_id, sizeof(prop_id));
    property->setId(prop_id[0] ? prop_id : id);
    property->setName(name);
    property->setDatatype(datatype);
    if (unit && unit[0] != '\0') property->setUnit(unit);
    if (format && format[0] != '\0') property->setFormat(format);  // enum/color/numeric (C2)
    if (settable) property->setSettable(true);
    if (!retained) property->setRetained(false);
    addProperty(property);
}

void Node::publish() {
    for(int i=0;i<_num_properties;i++) {
        _properties[i]->publish();
    }
}

void Node::clearRetained() {
    // Remove each property's retained value by publishing a zero-length payload.
    if (!_mqtt_client) return;
    for (int i = 0; i < _num_properties; i++) {
        _mqtt_client->publish(_properties[i]->topic(), "", true, homie_qos(true));
    }
}

void Node::setName(const char* name) {
    snprintf(_name, sizeof(_name), "%s", name);
}

const char* Node::name() {
    return _name;
}

void Node::setType(const char* type) {
    snprintf(_type, sizeof(_type), "%s", type);
}

const char* Node::type() {
    return _type;
}

void Node::setId(const char* id) {
    snprintf(_id, sizeof(_id), "%s", id);
}

const char* Node::id() {
    return _id;
}

void Node::setDevice(Device* device) {
   _device = device;
}

Device* Node::device() {
   return _device;
}

void Node::setTopic(const char* top) {
    snprintf(_topic, sizeof(_topic), "%s%s", top, _id);
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