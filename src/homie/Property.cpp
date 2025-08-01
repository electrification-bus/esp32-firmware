#include <config.h>
#include "homie/Property.h"
#include <homie/Node.h>
#include <homie/Device.h>
#include <mqtt_client.h>
#include <util/jsonUtils.h>

Property::Property() {
    _node = nullptr;
};

void Property::from_dict(JsonObject* props_obj) {

    if (jsonExists((*props_obj)[HOMIE_ID]) && (*props_obj)[HOMIE_ID].is<const char*>()) {
        setId((*props_obj)[HOMIE_ID].as<const char*>());
    }
    if (jsonExists((*props_obj)[HOMIE_NAME]) && (*props_obj)[HOMIE_NAME].is<const char*>()) {
        setName((*props_obj)[HOMIE_NAME].as<const char*>()); 
    }
    if (jsonExists((*props_obj)[HOMIE_DATATYPE]) && (*props_obj)[HOMIE_DATATYPE].is<const char*>()) {
        setDatatype((*props_obj)[HOMIE_DATATYPE].as<const char*>());
    }
    if (jsonExists((*props_obj)[HOMIE_UNIT]) && (*props_obj)[HOMIE_UNIT].is<const char*>()) {
        setUnit((*props_obj)[HOMIE_UNIT].as<const char*>());
    }
    if (jsonExists((*props_obj)[HOMIE_SETTABLE]) && (*props_obj)[HOMIE_SETTABLE].is<bool>()) {
        setSettable((*props_obj)[HOMIE_SETTABLE].as<bool>());
    }
    if (jsonExists((*props_obj)[HOMIE_RETAINED]) && (*props_obj)[HOMIE_RETAINED].is<bool>()) {
        setRetained((*props_obj)[HOMIE_RETAINED].as<bool>());
    }
    if (jsonExists((*props_obj)[HOMIE_VALUE])) {
        if ((*props_obj)[HOMIE_VALUE].is<bool>()) {
            setValue((*props_obj)[HOMIE_VALUE].as<bool>());
        }
        if ((*props_obj)[HOMIE_VALUE].is<float>()) {
            setValue((*props_obj)[HOMIE_VALUE].as<float>());
        }
        if ((*props_obj)[HOMIE_VALUE].is<const char*>()) {
            setValue((*props_obj)[HOMIE_VALUE].as<const char*>());
        }
        if ((*props_obj)[HOMIE_VALUE].is<int>()) {
            setValue((*props_obj)[HOMIE_VALUE].as<int>());
        }
        //TODO all types
    }

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

void Property::publish() {
    _mqtt_client->publish(topic(), _value, retained());
}

void Property::publish_target_value(const char* payload) {
    return publish(); //TODO ?
}

bool Property::publish_value() {
    return true; //TODO
}

void Property::device_new_value_callback(const char* sensor_value) {
    setValue(sensor_value);
}

void Property::mqtt_settable_callback(const char* topic, const char* payload) {
    bool isValid = false;
    if (strcmp(datatype(), HOMIE_DATATYPE_BOOLEAN) == 0) {
        //TODO allow various bools?
        if (strcmp(payload, "true") == 0 || strcmp(payload, "1") == 0 || strcmp(payload, "on") == 0 || strcmp(payload, "yes") == 0) {
            setValue(true);
        } else if (strcmp(payload, "false") == 0 || strcmp(payload, "0") == 0 || strcmp(payload, "off") == 0 || strcmp(payload, "no") == 0) {
            setValue(false);
        } else {
            Serial.printf("Node: '%s', Property: '%s' - invalid boolean value '%s'\n", _node->id(), _id, payload);
            return; //invalid value
        }; 
        isValid = true;
    } else if (strcmp(datatype(),  HOMIE_DATATYPE_STRING) == 0) {
        setValue(payload);
        isValid = true;
    } else if (strcmp(datatype(), HOMIE_DATATYPE_INTEGER) == 0) {
        setValue(atoi(payload));
        isValid = true;
    } else if (strcmp(datatype(), HOMIE_DATATYPE_FLOAT) == 0) {
        float f = atof(payload);
        setValue(f);
        isValid = true;
    }
    if (isValid) {
        Serial.printf("Node: '%s',  Property: '%s': datetype: '%s', new value: '%s'\n",_node->id(), _id, datatype(), payload);
        publish();
    }
}

void Property::subscribe() {
    if (!_mqtt_client || !_mqtt_client->connected()) {
        //TODO flag for retry
        return;
    }
    char set[128] = {0};
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
    subscribe_for_callbacks(set, &Property::mqtt_settable_callback, this);
}

const char* Property::topic() {
    return _topic;
}

void Property::setMQTTClient(PubSubClient* client) {
    _mqtt_client = client;
}

void Property::serialize(JsonDocument& json) {
    json[HOMIE_NAME] = _name;
    json[HOMIE_DATATYPE] = _datatype;
    if (_settable) {
        json[HOMIE_SETTABLE] = _settable;
    }
    if (!_retained) {
        json[HOMIE_RETAINED] = _retained;
    }
    if (strlen(_format) != 0) {
        json[HOMIE_FORMAT] = _format;
    }
    if (strlen(_unit) != 0) {
        json[HOMIE_UNIT] = _unit;
    }
}

void Property::register_for_device_callbacks(Node* node) {
   // node.property_map[_num_properties_mapped].funcPtr = callback;
    //strcpy(property_map[_num_properties_mapped++].prop_id, property_id);
   // Serial.printf("Node: '%s' - Property '%s' registered for device callbacks\n", _id, property_id);
}