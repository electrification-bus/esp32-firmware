#include <platform/config.h>
#include "homie/Property.h"
#include <homie/Node.h>
#include <homie/Device.h>
#include <homie/homie_datatype.h>
#include <platform/mqtt_client.h>
#include <util/jsonUtils.h>
#include <ctype.h>

Property::Property() {
    _parent_node = nullptr;
}

Property::Property(Node* node) {
    _parent_node = node;
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

// The whole-tree publish path (Device::publishTree -> Node::publish -> here). This one
// FORCES: it runs at boot and after a reconnect, where the point is to re-assert every
// value onto a broker that may have lost its retained store. Gating it would mean a
// reconnect republished nothing.
void Property::publish() {
    publish_value(true);
}

// Publish the current (coerced) value to the property topic. Returns the MQTT publish
// result; false (no publish) if no value has been set yet. (C5 — was a TODO stub.)
// ---- publish-on-change gate ----

bool Property::gate_allows(const char* payload, int len, bool force) {
    if (force) return true;
    if (!_retained) return true;              // event property: a repeat is a real event
    if (len > PROPERTY_MEMO_MAX) return true; // longer than the memo — never gate blind
    if (__atomic_load_n(&_in_flight, __ATOMIC_ACQUIRE) != 0) return true;
    if (_last_pub_len != len) return true;
    return memcmp(_last_pub, payload, len) != 0;
}

void Property::note_published(const char* payload, int len) {
    if (len > PROPERTY_MEMO_MAX) { _last_pub_len = -1; return; }  // unmemoable: forget
    memcpy(_last_pub, payload, len);
    _last_pub_len = len;
}

void Property::queued_publish_done(const char* payload, int len, bool sent) {
    if (sent) note_published(payload, len);
    __atomic_sub_fetch(&_in_flight, 1, __ATOMIC_RELEASE);
}

bool Property::publish_value(bool force) {
    if (!_has_value) return false;  // C3: don't publish a phantom retained-empty value topic
    // Empty-string VALUE -> single 0x00 byte; a zero-length payload would retract the
    // retained topic (Homie §Empty string values). Length-aware overload.
    static const char nul = 0x00;
    const char* payload = (_value[0] == '\0') ? &nul : _value;
    int len = (_value[0] == '\0') ? 1 : (int)strlen(_value);

    // A suppressed republish is a success: nothing failed and the broker holds the value.
    if (!gate_allows(payload, len, force)) return true;
    if (!_mqtt_client) return false;
    bool ok = _mqtt_client->publish(topic(), payload, len, retained(), homie_qos(retained()));
    if (ok) note_published(payload, len);
    return ok;
}

// Same value, same gate, but handed to the publish queue instead of the client — so a
// driver running on the NimBLE or Modbus task can publish without racing the MQTT client.
// The memo is not touched here: the queue reports back through queued_publish_done()
// once the client has actually sent the message, so a dropped one is not mistaken for
// delivered.
bool Property::publish_queued(bool force) {
    if (!_has_value) return false;
    static const char nul = 0x00;
    const char* payload = (_value[0] == '\0') ? &nul : _value;
    int len = (_value[0] == '\0') ? 1 : (int)strlen(_value);

    if (!gate_allows(payload, len, force)) return true;
    // Counted before the hand-off: the main task may send it before this call returns.
    __atomic_add_fetch(&_in_flight, 1, __ATOMIC_ACQ_REL);
    bool ok = mqtt_queue_publish(topic(), payload, len, retained(), this);
    if (ok) __atomic_add_fetch(&_queued_count, 1, __ATOMIC_RELAXED);
    return ok;
}

// Mark this property UNAVAILABLE: retract its retained topic (zero-length,
// retained => MQTT delete) rather than publishing a sentinel value. Resets _has_value
// so publish() won't re-emit the stale value until a fresh setValue().
void Property::clearValue() {
    _has_value = false;
    // Forget the memo: after a retraction the broker holds nothing, so the next set()
    // must publish even if it happens to repeat the value that was there before.
    _last_pub_len = -1;
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

// Reader for deserializeJson() that records how far the parser read. ArduinoJson stops
// at the bracket that closes the top-level array or object without reading past it, so
// anything left after _pos is trailing input that a whole-payload check must reject.
struct JsonPayloadReader {
    const char* _pos;
    const char* _end;
    int read() { return _pos < _end ? (unsigned char)*_pos++ : -1; }
    size_t readBytes(char* buffer, size_t length) {
        size_t n = 0;
        while (n < length && _pos < _end) buffer[n++] = *_pos++;
        return n;
    }
};

// Homie 5: a json property's payload MUST be a JSON array or object. True if the whole
// of `payload` (surrounding whitespace aside) is one. Logs the reason when it is not.
// The document is local, so the pool ArduinoJson allocates for it is freed on return.
static bool validate_json_payload(const char* node_id, const char* prop_id,
                                  const char* payload, size_t len) {
    JsonPayloadReader reader = { payload, payload + len };
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, reader);
    if (err) {
        Serial.printf("Node: '%s', Property: '%s' - invalid json payload '%s' (%s)\n",
                      node_id, prop_id, payload, err.c_str());
        return false;
    }
    while (reader._pos < reader._end && isspace((unsigned char)*reader._pos)) reader._pos++;
    if (reader._pos != reader._end) {
        Serial.printf("Node: '%s', Property: '%s' - invalid json payload '%s' (trailing input)\n",
                      node_id, prop_id, payload);
        return false;
    }
    if (!doc.is<JsonObject>() && !doc.is<JsonArray>()) {
        Serial.printf("Node: '%s', Property: '%s' - json payload '%s' is not an array or object\n",
                      node_id, prop_id, payload);
        return false;
    }
    return true;
}

// Validate a /set payload against the datatype and format and, if valid, store it.
// Returns false, with nothing stored, for an invalid payload. Publishing is left to
// dispatch_settable(), which first asks the driver (see publish_set()).
bool Property::store_set_payload(const char* payload) {
    bool isValid = false;
    if (strcmp(datatype(), HOMIE_DATATYPE_BOOLEAN) == 0) {
        //TODO allow various bools?
        if (strcmp(payload, "true") == 0 || strcmp(payload, "1") == 0 || strcmp(payload, "on") == 0 || strcmp(payload, "yes") == 0) {
            setValue(true);
        } else if (strcmp(payload, "false") == 0 || strcmp(payload, "0") == 0 || strcmp(payload, "off") == 0 || strcmp(payload, "no") == 0) {
            setValue(false);
        } else {
            Serial.printf("Node: '%s', Property: '%s' - invalid boolean value '%s'\n", _parent_node->id(), _id, payload);
            return false; //invalid value
        }; 
        isValid = true;
    } else if (strcmp(datatype(),  HOMIE_DATATYPE_STRING) == 0) {
        setValue(payload);
        isValid = true;
    } else if (strcmp(datatype(), HOMIE_DATATYPE_INTEGER) == 0) {
        // Check the payload's SYNTAX before its range. atoi() reported no failure, so
        // "abc" became 0 and was then range-checked as if the controller had sent 0,
        // while "+5" and "1.5" (read as 1) were accepted though the spec allows neither
        // for an integer. Then enforce the format's [min]:[max][:step] (C2); no format
        // => accept any value.
        int32_t parsed;
        double coerced;
        if (homie_parse_integer_payload(payload, &parsed) &&
            homie_validate_number((double)parsed, _format, &coerced)) {
            setValue((int)coerced); isValid = true;
        }
    } else if (strcmp(datatype(), HOMIE_DATATYPE_FLOAT) == 0) {
        // Same: atof() turned "abc" into 0.0, and would also have taken "0x10", "inf"
        // and "nan", none of which are Homie float payloads.
        double parsed;
        double coerced;
        if (homie_parse_float_payload(payload, &parsed) &&
            homie_validate_number(parsed, _format, &coerced)) {
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
    } else if (strcmp(datatype(), HOMIE_DATATYPE_JSON) == 0) {
        // Stored whole or not at all: setValue() would cut a longer payload at VALUE_MAX,
        // leaving a value that is no longer valid JSON.
        size_t len = strlen(payload);
        if (len > (size_t)VALUE_MAX) {
            Serial.printf("Node: '%s', Property: '%s' - json payload is %u chars, longer than "
                          "the %d a property holds; refused\n",
                          _parent_node->id(), _id, (unsigned)len, VALUE_MAX);
            return false;
        }
        if (!validate_json_payload(_parent_node->id(), _id, payload, len)) return false;
        setValue(payload);
        isValid = true;
    } else {
        Serial.printf("Node: '%s', Property: '%s' - unknown datatype '%s'; /set '%s' refused\n",
                      _parent_node->id(), _id, datatype(), payload);
        return false;
    }
    if (!isValid) {
        Serial.printf("Node: '%s', Property: '%s' - invalid %s payload '%s' (format '%s')\n",
                      _parent_node->id(), _id, datatype(), payload, _format);
    }
    if (isValid) {
        Serial.printf("Node: '%s',  Property: '%s': datetype: '%s', new value: '%s'\n",_parent_node->id(), _id, datatype(), payload);
    }
    return isValid;
}

// Report a /set that was stored and accepted. `value_queued` is true when the driver
// already queued a publish of this property while handling the set (see queued_count());
// that message reports the value, so only $target is published here.
void Property::publish_set(const char* payload, bool value_queued) {
    // C5: if this property supports $target, publish the EXACT received value to
    // $target first (byte-for-byte, closing the controller's control loop), then
    // publish the (coerced) property value. For instantaneous changes both happen
    // now; a slow transition would keep $target fixed and update the value over time.
    if (_supports_target) publish_target_value(payload);
    if (!value_queued) publish();
}

void Property::save_value(ValueSnapshot* s) const {
    memcpy(s->value, _value, sizeof(s->value));
    s->has_value = _has_value;
    s->bool_value = _boolValue;
    s->float_value = _floatValue;
    s->unsigned_value = _unsignedValue;
    s->int_value = _intValue;
}

void Property::restore_value(const ValueSnapshot* s) {
    memcpy(_value, s->value, sizeof(_value));
    _has_value = s->has_value;
    _boolValue = s->bool_value;
    _floatValue = s->float_value;
    _unsignedValue = s->unsigned_value;
    _intValue = s->int_value;
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
    subscribe_for_callbacks(set, &Property::store_set_payload, this);
}

const char* Property::topic() {
    return _topic;
}

void Property::setMQTTClient(MQTTClient* client) {
    _mqtt_client = client;
}

