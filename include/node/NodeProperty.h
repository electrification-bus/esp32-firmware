#pragma once
#include <homie/Node.h>
#include <homie/homie_id.h>
#include <platform/mqtt_client.h>
#include <platform/config.h>

// A driver's handle to one Homie property.
//
// NodeProperty holds NO value of its own. It resolves the Homie `Property` for
// <node_id>/<property_id> once in setup() and delegates everything to it, so there is
// exactly one place a property's value lives.
//
// It used to keep its own `_value[32]`, `_type`, `_format` and `_retained`, copied from
// the Property at setup() and never written back. Three things went wrong with that:
//
//   - Property::value() stayed frozen at whatever homie.yml said at boot, so one
//     component could not read another's live value (Device::getNode()->getProperty()).
//   - _type and _format were never assigned at all — dead fields behind live accessors.
//   - _value was 32 bytes against Property's 256, so long payloads were truncated
//     silently, one of two truncations in the old publish path.
//
// What NodeProperty still provides over using Property directly: typed set() overloads
// with the driver-facing number formatting, and the topic wiring. The formatting matters
// — set(float) publishes %.4f here, where Property::setValue(float) would use %f — so the
// typed overloads render text and hand Property a string rather than delegating the
// numeric overloads.

class NodeProperty {
    public:
        NodeProperty(){};
        ~NodeProperty(){};

        void setup(const char* node_id, const char* property_id) {
            // Resolve against the entity's OWN device (set by NodeEntity::setup() via
            // g_active_setup_device), so a nested child device's properties resolve under
            // the child rather than the root.
            Device* dev = g_active_setup_device ? g_active_setup_device : &theDevice;
            // Property-id is a topic level too — coerce to Homie-legal (a-z 0-9 -), e.g.
            // 'relative_humidity' -> 'relative-humidity'. The SAME sanitized id is used for
            // the topic AND the getProperty() lookup, so they stay in sync with the
            // Property's own sanitized id (set in Node::addProperty). (rrj.2)
            sanitize_homie_id(property_id, _homie_property_id, sizeof(_homie_property_id));
            if (_homie_property_id[0] == '\0') {
                snprintf(_homie_property_id, sizeof(_homie_property_id), "%s", property_id);
            }
            snprintf(_topic, sizeof(_topic), "%s%s/%s", dev->topic(), node_id, _homie_property_id);
            Serial.printf("NodeProperty: setup() topic: %s\n", _topic);

            Node* n = dev->getNode(node_id);
            _prop = n ? n->getProperty(_homie_property_id) : nullptr;
            if (!_prop) {
                // The generator registers a Property for everything declared in the
                // component's yml, so this means the driver called setup() for an id that
                // is not in its yml — a config error that would otherwise show up much
                // later as a property that silently never publishes.
                Serial.printf("NodeProperty: **ERROR -- no Homie property '%s' on node '%s' "
                              "— declare it in the component yml, or this will never "
                              "publish\n", _homie_property_id, node_id);
            }
        }

        void setup(const char* node_id, const char* property_id, entity_settable_callback_t cb, NodeEntity* ne) {
            setup(node_id, property_id);
            Device* dev = g_active_setup_device ? g_active_setup_device : &theDevice;
            char _top[160] = {0};
            // Use the sanitized id stored by setup() above, not the raw property_id, so
            // the /set subscription matches the published property topic.
            snprintf(_top, sizeof(_top), "%s%s/%s/%s", dev->topic(), node_id, homie_property_id(), HOMIE_TOPIC_SET);
            entity_subscribe_for_callbacks(_top, cb, ne);
        }

        // D3: register a per-property settable handler — a plain fn-ptr + opaque context
        // (no std::function, no lambda). Subscribes this property's /set topic and fires
        // handler(ctx, value) when a controller publishes. Call after setup() (needs the
        // built _topic). The property should also be declared settable so it appears in
        // $description; the author's static trampoline recovers `this` from ctx, e.g.:
        //   static void on_state(void* ctx, const char* v) { ((MyDrv*)ctx)->set(v); }
        //   _state.on_set(&MyDrv::on_state, this);
        void on_set(settable_handler_t handler, void* ctx) {
            char set_topic[192] = {0};
            snprintf(set_topic, sizeof(set_topic), "%s/%s", _topic, HOMIE_TOPIC_SET);
            if (mqtt_client.connected()) mqtt_client.subscribe(set_topic, 0);  // /set is QoS 0 (C4)
            subscribe_settable_handler(set_topic, handler, ctx);
        }

        const char* homie_property_id() const { return _homie_property_id; }
        void set_homie_property_id(const char* id) {
            snprintf(_homie_property_id, sizeof(_homie_property_id), "%s", id);
        }

        // Live reads, straight off the one value. Safe before setup() / with no Property.
        const char* value()  const { return _prop ? _prop->value()    : ""; }
        const char* type()   const { return _prop ? _prop->datatype() : ""; }
        const char* format() const { return _prop ? _prop->format()   : ""; }
        bool bound()         const { return _prop != nullptr; }
        Property* property() const { return _prop; }

        // ---- setters: render text, then store once on the Property ----------------
        // Rendering here (rather than calling Property's numeric overloads) preserves the
        // exact payloads drivers have always published — notably %.4f for float.
        void set(int value)          { char b[32]; snprintf(b, sizeof(b), "%d", value);   store(b); }
        void set(unsigned int value) { char b[32]; snprintf(b, sizeof(b), "%u", value);   store(b); }
        void set(float value)        { char b[32]; snprintf(b, sizeof(b), "%.4f", value); store(b); }
        void set(double value)       { char b[32]; snprintf(b, sizeof(b), "%f", value);   store(b); }
        void set(bool value)         { store(value ? "true" : "false"); }
        void set(const char* value)  { store(value); }

        void publish() {
            // Task-safe (goes through mqtt_queue) and gated on change: a retained property
            // whose payload is unchanged is not republished. See Property::publish_queued.
            if (_prop) _prop->publish_queued();
        }

        // rrj.4: mark this property UNAVAILABLE. Retracts the retained topic rather than
        // publishing a sentinel (0, -999, NaN) for a failed reading. Also clears the
        // change memo, so a later set() of the same value publishes again.
        void clearValue() { if (_prop) _prop->clearValue(); }

        void set_value_and_publish(int value)          { set(value); publish(); }
        void set_value_and_publish(unsigned int value) { set(value); publish(); }
        void set_value_and_publish(float value)        { set(value); publish(); }
        void set_value_and_publish(double value)       { set(value); publish(); }
        void set_value_and_publish(bool value)         { set(value); publish(); }
        void set_value_and_publish(const char* value)  { set(value); publish(); }

        const char* topic() const { return _topic; }
        void set_topic(const char* topic) { snprintf(_topic, sizeof(_topic), "%s", topic); }

    private:
        void store(const char* text) { if (_prop) _prop->setValue(text); }

        Property* _prop = nullptr;      // the one place this property's value lives
        char _homie_property_id[32] = {0};
        char _topic[160] = {0};
};
