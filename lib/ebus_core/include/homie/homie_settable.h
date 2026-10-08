#pragma once
// The settable table: every /set topic the device answers, with what handles it (the
// Homie Property that validates and stores the value, the driver behind it, or a plain
// handler), and the dispatch an inbound or local /set runs through.
#include <homie/homie_limits.h>

// Property and NodeEntity appear here ONLY as pointers and in pointer-to-member typedefs,
// both of which an incomplete type satisfies.
class Property;
class NodeEntity;

// Validates and stores a /set payload on the Homie property (Property::store_set_payload).
typedef bool (Property::*property_settable_callback_t)(const char* payload);
// Driver settable callbacks return true when they accepted (applied) the value and false
// when they refused it. Only an accepted value is published; a refused one is put back.
typedef bool (NodeEntity::*entity_settable_callback_t)(Property*);

// Per-settable handler (D3): a plain function pointer + opaque context — the STL-free,
// heap-free callback idiom (no std::function, no lambda). The author registers a static
// trampoline and passes `this` as ctx to recover instance state. value is the decoded
// /set payload. Returns true to accept the value, false to refuse it.
typedef bool (*settable_handler_t)(void* ctx, const char* value);

// EVERY member needs a default initializer. The dispatch tests each callback pointer for
// null before using it, so an indeterminate one is not "unused garbage" — it is a branch
// taken into a junk pointer-to-member. Three functions register entries here and each
// sets only the fields it cares about, so whatever the others are is what dispatch sees.
struct subscribed_settable_property_t {
    char topic[HOMIE_TOPIC_MAX + 1] = {0};
    property_settable_callback_t property_callback = nullptr;
    entity_settable_callback_t entity_callback = nullptr;
    Property* instance = nullptr; // store the instance to call the member function on
    NodeEntity* entity = nullptr; // Pointer to the associated hardware entity
    settable_handler_t handler = nullptr;   // D3: per-property fn-ptr handler
    void* handler_ctx = nullptr;            // D3: opaque context for handler
};

void subscribe_for_callbacks(const char* topic, property_settable_callback_t cb, Property* instance);
void entity_subscribe_for_callbacks(const char* topic, entity_settable_callback_t cb, NodeEntity* entity);
// D3: register a per-property fn-ptr handler (+context) for a /set topic.
void subscribe_settable_handler(const char* topic, settable_handler_t handler, void* ctx);

// Set a settable property from INSIDE the firmware, with no broker round-trip: the value
// is delivered straight to the same handler an inbound /set would reach, so it still works
// while MQTT is down. `topic` is the full /set topic
// ("homie/5/<device-id>/<node>/<property>/set"). Returns false when no settable property
// is registered for that topic (a config error — wrong node/property id — not a
// transient one).
//
// This is how one component actuates another: address the target as <node>/<property>,
// the same pair an MQTT controller would publish to, rather than holding a pointer to it.
// A pointer can only be wired from a hand-coded src/homie.cpp (the generator cannot emit
// cross-node links), which is why examples/bms still carries setContactor().
//
// Call it from the main task only: it runs the same dispatch as an inbound /set, which
// snapshots and restores the property's value without a lock. A true return means the
// set was delivered, not that the driver accepted it.
bool deliver_local_set(const char* topic, const char* value);
