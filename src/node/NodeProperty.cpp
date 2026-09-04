#include <Arduino.h>
#include <node/NodeProperty.h>
#include <homie/Device.h>     // theDevice, g_active_setup_device
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
    // sanitized id (set in Node::addProperty). (rrj.2)
    sanitize_homie_id(property_id, _homie_property_id, sizeof(_homie_property_id));
    if (_homie_property_id[0] == '\0') {
        snprintf(_homie_property_id, sizeof(_homie_property_id), "%s", property_id);
    }
    snprintf(_topic, sizeof(_topic), "%s%s/%s", dev->topic(), node_id, _homie_property_id);
    Serial.printf("NodeProperty: setup() topic: %s\n", _topic);

    Node* n = dev->getNode(node_id);
    _prop = n ? n->getProperty(_homie_property_id) : nullptr;
    if (!_prop) {
        // The generator registers a Property for everything declared in the component's
        // yml, so this means the driver called setup() for an id that is not in its yml —
        // a config error that would otherwise surface much later as a property that
        // silently never publishes.
        Serial.printf("NodeProperty: **ERROR -- no Homie property '%s' on node '%s' — "
                      "declare it in the component yml, or this will never publish\n",
                      _homie_property_id, node_id);
    }
}

void NodeProperty::setup(const char* node_id, const char* property_id,
                         entity_settable_callback_t cb, NodeEntity* ne) {
    setup(node_id, property_id);
    Device* dev = g_active_setup_device ? g_active_setup_device : &theDevice;
    char set_topic[192] = {0};
    // Use the sanitized id stored by setup() above, not the raw property_id, so the /set
    // subscription matches the published property topic.
    snprintf(set_topic, sizeof(set_topic), "%s%s/%s/%s",
             dev->topic(), node_id, homie_property_id(), HOMIE_TOPIC_SET);
    entity_subscribe_for_callbacks(set_topic, cb, ne);
}

void NodeProperty::on_set(settable_handler_t handler, void* ctx) {
    char set_topic[192] = {0};
    snprintf(set_topic, sizeof(set_topic), "%s/%s", _topic, HOMIE_TOPIC_SET);
    if (mqtt_client.connected()) mqtt_client.subscribe(set_topic, 0);  // /set is QoS 0 (C4)
    subscribe_settable_handler(set_topic, handler, ctx);
}

// ---- reads ------------------------------------------------------------------

const char* NodeProperty::value()  const { return _prop ? _prop->value()    : ""; }
const char* NodeProperty::type()   const { return _prop ? _prop->datatype() : ""; }
const char* NodeProperty::format() const { return _prop ? _prop->format()   : ""; }

// ---- writes -----------------------------------------------------------------

void NodeProperty::store(const char* text) {
    if (_prop) _prop->setValue(text);
}

// Rendering here (rather than calling Property's numeric overloads) preserves the exact
// payloads drivers have always published — notably %.4f for float, where
// Property::setValue(float) would emit %f.
void NodeProperty::set(int value)          { char b[32]; snprintf(b, sizeof(b), "%d", value);   store(b); }
void NodeProperty::set(unsigned int value) { char b[32]; snprintf(b, sizeof(b), "%u", value);   store(b); }
void NodeProperty::set(float value)        { char b[32]; snprintf(b, sizeof(b), "%.4f", value); store(b); }
void NodeProperty::set(double value)       { char b[32]; snprintf(b, sizeof(b), "%f", value);   store(b); }
void NodeProperty::set(bool value)         { store(value ? "true" : "false"); }
void NodeProperty::set(const char* value)  { store(value); }

void NodeProperty::publish() {
    if (_prop) _prop->publish_queued();
}

void NodeProperty::clearValue() {
    if (_prop) _prop->clearValue();
}

void NodeProperty::set_value_and_publish(int value)          { set(value); publish(); }
void NodeProperty::set_value_and_publish(unsigned int value) { set(value); publish(); }
void NodeProperty::set_value_and_publish(float value)        { set(value); publish(); }
void NodeProperty::set_value_and_publish(double value)       { set(value); publish(); }
void NodeProperty::set_value_and_publish(bool value)         { set(value); publish(); }
void NodeProperty::set_value_and_publish(const char* value)  { set(value); publish(); }
