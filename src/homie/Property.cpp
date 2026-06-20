#include <platform/config.h>
#include "homie/Property.h"
#include <homie/Node.h>
#include <homie/Device.h>
#include <homie/homie_datatype.h>
#include <platform/mqtt_client.h>
#include <util/jsonUtils.h>

Property::Property() {
    _parent_node = nullptr;
}

Property::Property(Node* node) {
    _parent_node = node;
}

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
    if (jsonExists((*props_obj)[HOMIE_FORMAT]) && (*props_obj)[HOMIE_FORMAT].is<const char*>()) {
        setFormat((*props_obj)[HOMIE_FORMAT].as<const char*>());  // required for enum/color (C1)
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
    _parent_node = node;
}

Node* Property::node() {
    return _parent_node;
}

void Property::setId(const char* id) {
    snprintf(_id, sizeof(_id), "%s", id);
    // Requires setNode() first — dereferences _parent_node to build topic
    snprintf(_topic, sizeof(_topic), "%s/%s", _parent_node->topic(), _id);
}

const char* Property::id() const {
    return _id;
}

void Property::setUnit(const char* unit) {
    snprintf(_unit, sizeof(_unit), "%s", unit);
}

const char* Property::unit() {
    return _unit;
}

void Property::setFormat(const char* fmt) {
    snprintf(_format, sizeof(_format), "%s", fmt);
}

const char* Property::format() {
    return _format;
}

void Property::setName(const char* name) {
    snprintf(_name, sizeof(_name), "%s", name);
}

const char* Property::name() const {
    return _name;
}

void Property::setValue(int value) {
    _intValue = value;
    snprintf(_value, sizeof(_value), "%d", value);
    _has_value = true;
}
void Property::setValue(float value) {
    _floatValue = value;
    snprintf(_value, sizeof(_value), "%f", value);
    _has_value = true;
}
void Property::setValue(const char* value) {
    snprintf(_stringValue, sizeof(_stringValue), "%s", value);
    snprintf(_value, sizeof(_value), "%s", value);
    _has_value = true;
}
void Property::setValue(bool value) {
    _boolValue = value;
    strcpy(_value, value ? "true" : "false");
    _has_value = true;
}
void Property::setValue(unsigned int value) {
    _unsignedValue = value;
    snprintf(_value, sizeof(_value), "%u", value);
    _has_value = true;
}

const char* Property::coerced_value() const {
    // _value already holds the coerced form: numeric settables are step-rounded and
    // range-checked before storage (C2), enum/color/datetime/duration store the
    // validated payload. So the canonical stored value IS the coerced value.
    return _value;
}

void Property::setDatatype(const char* dt)  {
    strncpy(_datatype, dt, sizeof(_datatype) - 1);
    _datatype[sizeof(_datatype) - 1] = '\0';
}

const char* Property::datatype() const {
    return _datatype;
}

MQTTClient* Property::mqttClient() const {
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
    return strcmp(_datatype, HOMIE_DATATYPE_JSON) == 0;
}

void Property::setSupportsTarget(bool t) {
    _supports_target = t;
}

bool Property::supportsTarget() {
    return _supports_target;
}
void Property::set_callback() const{
}

void Property::publish() {
    publish_value();
}

// Publish the current (coerced) value to the property topic. Returns the MQTT publish
// result; false (no publish) if no value has been set yet. (C5 — was a TODO stub.)
bool Property::publish_value() {
    if (!_has_value) return false;  // C3: don't publish a phantom retained-empty value topic
    if (_value[0] == '\0') {
        // Empty-string VALUE -> single 0x00 byte; a zero-length payload would retract
        // the retained topic (Homie §Empty string values). Length-aware overload.
        static const char nul = 0x00;
        return _mqtt_client->publish(topic(), &nul, 1, retained(), homie_qos(retained()));
    }
    return _mqtt_client->publish(topic(), _value, retained(), homie_qos(retained()));
}

// Mark this property UNAVAILABLE (rrj.4): retract its retained topic (zero-length,
// retained => MQTT delete) rather than publishing a sentinel value. Resets _has_value
// so publish() won't re-emit the stale value until a fresh setValue().
void Property::clearValue() {
    _has_value = false;
    if (_mqtt_client) _mqtt_client->publish(topic(), "", true, homie_qos(true));
}

// Publish the intended target value to the property's $target topic (C5). Per spec,
// the EXACT value received on /set is published byte-for-byte (no coercion), retained,
// so a controller can close its control loop. Only called when supportsTarget().
void Property::publish_target_value(const char* payload) {
    char target_topic[96] = {0};
    snprintf(target_topic, sizeof(target_topic), "%s/%s", _topic, HOMIE_$TARGET);
    _mqtt_client->publish(target_topic, payload, true, homie_qos(true));
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
            Serial.printf("Node: '%s', Property: '%s' - invalid boolean value '%s'\n", _parent_node->id(), _id, payload);
            return; //invalid value
        }; 
        isValid = true;
    } else if (strcmp(datatype(),  HOMIE_DATATYPE_STRING) == 0) {
        setValue(payload);
        isValid = true;
    } else if (strcmp(datatype(), HOMIE_DATATYPE_INTEGER) == 0) {
        // Enforce the format's [min]:[max][:step] (C2). No format => accept as-is.
        double coerced;
        if (homie_validate_number((double)atoi(payload), _format, &coerced)) {
            setValue((int)coerced); isValid = true;
        }
    } else if (strcmp(datatype(), HOMIE_DATATYPE_FLOAT) == 0) {
        double coerced;
        if (homie_validate_number(atof(payload), _format, &coerced)) {
            setValue((float)coerced); isValid = true;
        }
    } else if (strcmp(datatype(), HOMIE_DATATYPE_ENUM) == 0) {
        // Payload must be one of the comma-separated values in the property's format.
        if (homie_validate_enum(payload, _format)) { setValue(payload); isValid = true; }
    } else if (strcmp(datatype(), HOMIE_DATATYPE_COLOR) == 0) {
        // "<type>,<floats>" with type in the format list and per-type ranges.
        if (homie_validate_color(payload, _format)) { setValue(payload); isValid = true; }
    } else if (strcmp(datatype(), HOMIE_DATATYPE_DATETIME) == 0) {
        if (homie_validate_datetime(payload)) { setValue(payload); isValid = true; }
    } else if (strcmp(datatype(), HOMIE_DATATYPE_DURATION) == 0) {
        if (homie_validate_duration(payload)) { setValue(payload); isValid = true; }
    }
    if (!isValid && strlen(datatype()) > 0) {
        Serial.printf("Node: '%s', Property: '%s' - invalid %s payload '%s' (format '%s')\n",
                      _parent_node->id(), _id, datatype(), payload, _format);
    }
    if (isValid) {
        Serial.printf("Node: '%s',  Property: '%s': datetype: '%s', new value: '%s'\n",_parent_node->id(), _id, datatype(), payload);
        // C5: if this property supports $target, publish the EXACT received value to
        // $target first (byte-for-byte, closing the controller's control loop), then
        // publish the (coerced) property value. For instantaneous changes both happen
        // now; a slow transition would keep $target fixed and update the value over time.
        if (_supports_target) publish_target_value(payload);
        publish();
    }

}

void Property::subscribe() {
    if (!_mqtt_client || !_mqtt_client->connected()) {
        //TODO flag for retry
        return;
    }
    char set[128] = {0};
    snprintf(set, sizeof(set), "%s/%s", _topic, HOMIE_TOPIC_SET);
    Serial.printf("property '%s' settable - subscribe: '%s'\n",_id, set);
    //TODO pull this out; retry in loop
    if (!_mqtt_client->subscribe(set, 0)) {   // /set is non-retained -> QoS 0 (C4)
        delay(250);
        if (!_mqtt_client->subscribe(set, 0)) {   // /set is non-retained -> QoS 0 (C4)
            delay(500);
            if (!_mqtt_client->subscribe(set, 0)) {   // /set is non-retained -> QoS 0 (C4)
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

void Property::setMQTTClient(MQTTClient* client) {
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

void Property::serializeInto(JsonObject& obj) {
    obj[HOMIE_NAME] = _name;
    obj[HOMIE_DATATYPE] = _datatype;
    if (_settable) obj[HOMIE_SETTABLE] = _settable;
    if (!_retained) obj[HOMIE_RETAINED] = _retained;
    if (strlen(_format) != 0) obj[HOMIE_FORMAT] = _format;
    if (strlen(_unit) != 0) obj[HOMIE_UNIT] = _unit;
}