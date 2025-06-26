#include <config.h>
#include "homie/Property.h"
#include <homie/Node.h>
#include <homie/Device.h>
#include <mqtt_client.h>
Property::Property() {
    _node = nullptr;
};

void Property::from_dict(JsonObject* props_obj) {
    if ((*props_obj)[HOMIE_ID].is<const char*>()) {
        setId((*props_obj)[HOMIE_ID].as<const char*>());
    }
    if ((*props_obj)[HOMIE_NAME].is<const char*>()) {
        setName((*props_obj)[HOMIE_NAME].as<const char*>()); 

    }
    if ((*props_obj)[HOMIE_DATATYPE].is<const char*>()) {
        setDatatype((*props_obj)[HOMIE_DATATYPE].as<const char*>());
    }
    if ((*props_obj)[HOMIE_VALUE].is<bool>()) {
        setValue((*props_obj)[HOMIE_VALUE].as<bool>());
    }
    if ((*props_obj)[HOMIE_VALUE].is<float>()) {
        setValue((*props_obj)[HOMIE_VALUE].as<float>());
    }
    if ((*props_obj)[HOMIE_VALUE].is<const char*>()) {
        setValue((*props_obj)[HOMIE_VALUE].as<const char*>());
    }
    
    //setFormat((*props_obj)["format"].as<const char*>());
    setSettable((*props_obj)[HOMIE_SETTABLE].as<bool>());
}

void Property::setNode(Node* node) {
    _node = node;
}

Node* Property::node() {
    return _node;
}

void Property::setId(const char* id) {
    strcpy(_id, id);
    //SET-ID SIDE-EFFECT - construct the topic
    sprintf(_topic, "%s/%s", _node->topic(), _id);
}

const char* Property::id() const {
    return _id;
}

void Property::setUnit(const char* unit) {
    strcpy(_unit, unit);
}

const char* Property::unit() {
    return _unit;
}

void Property::setName(const char* name) {
    strcpy(_name, name);
}

const char* Property::name() const {
    return _name;
}

void Property::setValue(int value) {
    _intValue = value;
    sprintf(_value, "%d", value);
}
void Property::setValue(float value) {
    _floatValue = value;
    snprintf(_value, sizeof(_value), "%f", value);
}
void Property::setValue(const char* value) {
    strcpy(_stringValue, value);
    strcpy(_value, value);
}
void Property::setValue(bool value) {
    _boolValue = value;
    strcpy(_value, value ? "true" : "false");
}
void Property::setValue(unsigned int value) {
    _unsignedValue = value;
    sprintf(_value, "%u", value);
}

void Property::setFormat(const char* fmt) {
    strcpy(_format, fmt);
}

const char* Property::value() {
    return _value;
}
const char* Property::coerced_value() const {
    return _value; //TODO
}

void Property::setDatatype(const char* dt)  {
    strcpy(_datatype, dt);
}

const char* Property::datatype() const {
    return _datatype;
}

PubSubClient* Property::mqttClient() const {
    return _mqtt_client;
}

void Property::start_mqtt_client() {}


void Property::setSettable(bool s) {
   _settable = s;
}
bool Property::settable() {
    return _settable;
}

void Property::setRetained(bool r) {
    _retained = r;
}

bool Property::retained() {
    return _retained;
}

bool Property::is_json_datatype() const {
    return false;
}
void Property::set_callback() const{
}

void Property::publish_target_value(const char* payload) {
    return publish(); //TODO ?
}

bool Property::publish_value() {
    return true; //TODO
}

void Property::_settable_callback(const char* topic, const char* payload) {
    Serial.printf("Node: '%s',  Property: '%s': new value:'%s'\n",_node->id(), _id, payload);
    //TODO set _value
}

void Property::subscribe() {
    if (!_mqtt_client || !_mqtt_client->connected()) {
        //TODO flag for retry
        return;
    }
    char set[64] = {0};
    sprintf(set, "%s/%s", _topic, HOMIE_TOPIC_SET);
    Serial.printf("property '%s' settable - subscribe: '%s'\n",_id, set);
    //TODO pull this out; retry in loop
    if (!_mqtt_client->subscribe(set)) {
        delay(250);
        if (!_mqtt_client->subscribe(set)) {
            delay(500);
            if (!_mqtt_client->subscribe(set)) {
                Serial.printf("MQTT: FAILED TO SUBSCRIBE TO PROPERTY SET TOPIC: %s\r\n", set);
                return;
            }
        }
    }
    //register the property for callbacks
    subscribe_for_callbacks(set, &Property::_settable_callback, this);
}

const char* Property::topic() {
    return _topic;
}

void Property::setMQTTClient(PubSubClient* client) {
    _mqtt_client = client;
}

void Property::publish() {
    _mqtt_client->publish(topic(), _value); //TODO retained flag
}

JsonDocument Property::serialize() {
    JsonDocument json;
    json[HOMIE_ID] = _id;
    json[HOMIE_NAME] = _name;
    json[HOMIE_DATATYPE] = _datatype;
    json[HOMIE_FORMAT] = _format;
    json[HOMIE_SETTABLE] = _settable;
    json[HOMIE_RETAINED] = _retained;
    json[HOMIE_UNIT] = _unit;
    json[HOMIE_ROUNDTO] = 0;
    return json;
}