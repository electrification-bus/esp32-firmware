#pragma once
// Included ONLY for the entity_settable_callback_t / settable_handler_t typedefs in the
// signatures below. It forward-declares Property rather than including it, and that is
// all the Property* member needs.
//
// Deliberately NOT <platform/config.h>: the bodies need theDevice and
// g_active_setup_device, but config.h drags util/jsonUtils.h and homie/homie.h behind
// it, and NodeProperty.h reaches every driver through NodeEntity.h.
// Those includes belong in NodeProperty.cpp, where they are actually used.
#include <platform/mqtt_client.h>

class NodeEntity;

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
// numeric overloads. See NodeProperty.cpp.

class NodeProperty {
    public:
        NodeProperty(){};
        ~NodeProperty(){};

        // Resolve this handle against the entity's own device and build the topic. The
        // four-argument form additionally subscribes the property's /set topic and routes
        // it to the entity's settable callback.
        void setup(const char* node_id, const char* property_id);
        void setup(const char* node_id, const char* property_id,
                   entity_settable_callback_t cb, NodeEntity* ne);

        // D3: register a per-property settable handler — a plain fn-ptr + opaque context
        // (no std::function, no lambda). Subscribes this property's /set topic and fires
        // handler(ctx, value) when a controller publishes. Call after setup() (needs the
        // built topic). The property should also be declared settable so it appears in
        // $description; the author's static trampoline recovers `this` from ctx and returns
        // whether the value was accepted (see settable_handler_t), e.g.:
        //   static bool on_state(void* ctx, const char* v) { return ((MyDrv*)ctx)->apply(v); }
        //   _state.on_set(&MyDrv::on_state, this);
        void on_set(settable_handler_t handler, void* ctx);

        // Live reads, straight off the one value. Safe before setup() / with no Property.
        const char* value()  const;
        const char* type()   const;
        const char* format() const;

        // ---- setters: render text, then store once on the Property ----------------
        //
        // CAST an int32_t, uint32_t, long, unsigned long or size_t argument. On Xtensa
        // those are `long`, which converts to int, int64_t, unsigned int, float, double
        // AND bool at the same (Conversion) rank, so the call is ambiguous and will not
        // compile — say which one you mean, e.g. set((int64_t)n). Applies equally to
        // set_value_and_publish() below. Not a consequence of the int64_t overload:
        // `long` already tied across the others before it existed.
        void set(int value);
        void set(int64_t value);      // Homie 5 integers are 64-bit signed
        void set(unsigned int value);
        void set(float value);
        void set(double value);
        void set(bool value);
        void set(const char* value);

        // Task-safe (goes through mqtt_queue) and gated on change: a retained property
        // whose payload is unchanged is not republished. See Property::publish_queued.
        void publish();

        // mark this property UNAVAILABLE. Retracts the retained topic rather than
        // publishing a sentinel (0, -999, NaN) for a failed reading. Also clears the
        // change memo, so a later set() of the same value publishes again.
        void clearValue();

        void set_value_and_publish(int value);
        void set_value_and_publish(int64_t value);
        void set_value_and_publish(unsigned int value);
        void set_value_and_publish(float value);
        void set_value_and_publish(double value);
        void set_value_and_publish(bool value);
        void set_value_and_publish(const char* value);

        // Trivial member access — kept inline; they touch no other type.
        const char* homie_property_id() const { return _homie_property_id; }
        void set_homie_property_id(const char* id) {
            snprintf(_homie_property_id, sizeof(_homie_property_id), "%s", id);
        }
        const char* topic() const { return _topic; }
        void set_topic(const char* t) { snprintf(_topic, sizeof(_topic), "%s", t); }
        bool bound() const { return _prop != nullptr; }
        Property* property() const { return _prop; }

    private:
        void store(const char* text);
        void report_unbound(const char* what);

        Property* _prop = nullptr;      // the one place this property's value lives
        bool _unbound_reported = false; // report_unbound() has logged for this handle
        char _homie_property_id[32] = {0};
        char _topic[160] = {0};
};
