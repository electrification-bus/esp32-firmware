#include <Arduino.h>
#include <node/NodeProperty.h>
#include <homie/Device.h>
#include <homie/homie_globals.h>   // theDevice, g_active_setup_device
#include <homie/Node.h>
#include <homie/homie_id.h>

// ---- setup ------------------------------------------------------------------

void NodeProperty::setup(const char* node_id, const char* property_id) {
    // Resolve against the entity's OWN device (set by NodeEntity::setup() via
    // g_active_setup_device), so a nested child device's properties resolve under the
    // child rather than the root.
    Device* dev = g_active_setup_device ? g_active_setup_device : &theDevice;

    // Property-id is a topic level too — coerce to Homie-legal (a-z 0-9 -), e.g.
    // 'relative_humidity' -> 'relative-humidity'. The SAME sanitized id is used for the
    // topic AND the getProperty() lookup, so they stay in sync with the Property's own
    // sanitized id (set in Node::addProperty).
    if (!sanitize_homie_id(property_id, _homie_property_id, sizeof(_homie_property_id))) {
        Serial.printf("NodeProperty: **ERROR -- property id '%s' is over %d chars; truncated "
                      "to '%s'\n", property_id, HOMIE_PROPERTY_ID_MAX, _homie_property_id);
    }
    if (_homie_property_id[0] == '\0') {
        snprintf(_homie_property_id, sizeof(_homie_property_id), "%s", property_id);
    }
    int len = snprintf(_topic, sizeof(_topic), "%s%s/%s", dev->topic(), node_id, _homie_property_id);
    if (len >= (int)sizeof(_topic)) {
        Serial.printf("NodeProperty: **ERROR -- topic for '%s/%s' is %d chars, over %d; "
                      "truncated\n", node_id, _homie_property_id, len, (int)sizeof(_topic) - 1);
    }
    Serial.printf("NodeProperty: setup() topic: %s\n", _topic);

    Node* n = dev->getNode(node_id);
    _prop = n ? n->getProperty(_homie_property_id) : nullptr;
    _unbound_reported = false;
    if (!_prop) {
        // The generator registers a Property for everything declared in the component's
        // yml, so this means the driver called setup() for an id that is not in its yml
        // (or the node itself is missing). Every set and publish on this handle is then
        // dropped, so say so at boot, and again on the first dropped write (report_unbound).
        Serial.printf("\n"
                      "NodeProperty: **ERROR ******************************************\n"
                      "NodeProperty: **ERROR -- %s '%s' has no Homie property '%s'\n"
                      "NodeProperty: **ERROR -- declare '%s' in the component yml; until then\n"
                      "NodeProperty: **ERROR -- every value the driver sets on it is DROPPED\n"
                      "NodeProperty: **ERROR ******************************************\n\n",
                      n ? "node" : "MISSING node", node_id, _homie_property_id,
                      _homie_property_id);
    }
}

// Once per handle, so a driver that sets the property every second does not flood the log.
void NodeProperty::report_unbound(const char* what) {
    if (_unbound_reported) return;
    _unbound_reported = true;
    Serial.printf("NodeProperty: **ERROR -- %s on '%s' DROPPED: property not declared in the "
                  "component yml (see the setup error above)\n", what, _topic);
}

void NodeProperty::setup(const char* node_id, const char* property_id,
                         entity_settable_callback_t cb, NodeEntity* ne) {
    setup(node_id, property_id);
    Device* dev = g_active_setup_device ? g_active_setup_device : &theDevice;
    char set_topic[HOMIE_TOPIC_MAX + 1] = {0};
    // Use the sanitized id stored by setup() above, not the raw property_id, so the /set
    // subscription matches the published property topic.
    snprintf(set_topic, sizeof(set_topic), "%s%s/%s/%s",
             dev->topic(), node_id, homie_property_id(), HOMIE_TOPIC_SET);
    entity_subscribe_for_callbacks(set_topic, cb, ne);
}

void NodeProperty::on_set(settable_handler_t handler, void* ctx) {
    char set_topic[HOMIE_TOPIC_MAX + 1] = {0};
    snprintf(set_topic, sizeof(set_topic), "%s/%s", _topic, HOMIE_TOPIC_SET);
    settable_subscribe_now(set_topic);   // /set is QoS 0 (C4)
    subscribe_settable_handler(set_topic, handler, ctx);
}

// ---- reads ------------------------------------------------------------------

const char* NodeProperty::value()  const { return _prop ? _prop->value()    : ""; }
const char* NodeProperty::type()   const { return _prop ? _prop->datatype() : ""; }
const char* NodeProperty::format() const { return _prop ? _prop->format()   : ""; }

// ---- writes -----------------------------------------------------------------

void NodeProperty::store(const char* text) {
    if (_prop) _prop->setValue(text);
    else report_unbound("set");
}

// Rendering here (rather than calling Property's numeric overloads) preserves the exact
// payloads drivers have always published — notably %.4f for float, where
// Property::setValue(float) would emit %f.
void NodeProperty::set(int value)          { char b[32]; snprintf(b, sizeof(b), "%d", value);   store(b); }
void NodeProperty::set(int64_t value)      { char b[32]; snprintf(b, sizeof(b), "%lld", (long long)value); store(b); }
void NodeProperty::set(unsigned int value) { char b[32]; snprintf(b, sizeof(b), "%u", value);   store(b); }
void NodeProperty::set(float value)        { char b[32]; snprintf(b, sizeof(b), "%.4f", value); store(b); }
void NodeProperty::set(double value)       { char b[32]; snprintf(b, sizeof(b), "%f", value);   store(b); }
void NodeProperty::set(bool value)         { store(value ? "true" : "false"); }
void NodeProperty::set(const char* value)  { store(value); }

void NodeProperty::publish() {
    if (_prop) _prop->publish_queued();
    else report_unbound("publish");
}

void NodeProperty::clearValue() {
    if (_prop) _prop->clearValue();
}

void NodeProperty::set_value_and_publish(int value)          { set(value); publish(); }
void NodeProperty::set_value_and_publish(int64_t value)      { set(value); publish(); }
void NodeProperty::set_value_and_publish(unsigned int value) { set(value); publish(); }
void NodeProperty::set_value_and_publish(float value)        { set(value); publish(); }
void NodeProperty::set_value_and_publish(double value)       { set(value); publish(); }
void NodeProperty::set_value_and_publish(bool value)         { set(value); publish(); }
void NodeProperty::set_value_and_publish(const char* value)  { set(value); publish(); }
