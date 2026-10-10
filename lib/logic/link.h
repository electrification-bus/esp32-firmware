#pragma once
#include <node/NodeEntity.h>

class Link;

// One source of a Link. A device build's remote source is written into `value` by
// on_remote_value(), which settable_dispatch() calls from mqtt_process_queue() on the main
// task, the task loop() runs on, so a plain write is safe. The receive callback only
// queues the message (MQTT_QUEUE_SET).
struct LinkSource {
    static const size_t VALUE_MAX = 64;

    Link* owner = nullptr;
    bool  remote = false;
    char  dev[HOMIE_DEVICE_ID_MAX + 1]    = {0};   // remote only: the other device's id
    char  node[HOMIE_NODE_ID_MAX + 1]     = {0};
    char  prop[HOMIE_PROPERTY_ID_MAX + 1] = {0};
    char  value[VALUE_MAX] = {0};       // last good (non-empty) value
    bool  has_value = false;
    // `dev` (and possibly `node`) was written as a * pattern and has not been bound to a
    // real device yet. Both are overwritten with the concrete ids once it resolves;
    // `refused` means it matched more than one device and will not be retried.
    bool  wild    = false;
    bool  refused = false;
    bool  unmatched = false;            // the pattern matched nothing at the last attempt
};

// Copies property values into a settable property — sensor readings onto a display, a
// state onto a relay — configured entirely in device.yml.
//
//   source  up to MAX_SOURCES references, comma-separated, each one of
//             <node>/<property>              a property of THIS device, read locally
//             <device-id>/<node>/<property>  a property of ANOTHER device, read off the
//                                            broker (subscribed; the retained value
//                                            arrives on connect)
//             */<node>/<property>            CONTROLLER BUILDS: the one discovered device
//                                            that has that property. `*` may also appear
//                                            inside the node, as */environment-*/..., but
//                                            never in the property. Bound once discovery
//                                            has settled; see doc/Link.md.
//   target  <node>/<property>                a settable property of this device, driven
//                                            with deliver_local_set() — no broker needed
//           <device-id>/<node>/<property>    a settable property of another device,
//                                            driven by publishing to its /set topic
//
//   format       text sent to the target: %1, %2, %3 become the sources' values in order,
//                %s is %1, %% is a literal '%'. Substituted by hand, never passed to printf.
//   decimals     round numeric values to this many places first (21.37 -> "21.4" at 1);
//                -1 sends them as published. Non-numeric values pass through.
//   interval_ms  how often LOCAL sources are polled. Remote values are pushed by the
//                broker and handled on the next loop() regardless.
//
// Nothing is sent until every source has had a value, so the target never shows a blank
// where a reading belongs (no sentinels). After that a source that goes empty — retracted
// — keeps its last good value. The target is written only when the rendered text changes,
// and retried if delivery failed (broker down for a remote target, target not registered
// yet), after interval_ms and then twice as long each time, up to 30 s.
class Link : public NodeEntity {
    public:
        static const int MAX_SOURCES = 3;

        Link(const char* homie_node_id, const char* source, const char* target,
             const char* format, int decimals, uint32_t interval_ms)
            : NodeEntity(homie_node_id), _source(source), _target(target),
              _format(format), _decimals(decimals), _interval_ms(interval_ms) {}
        ~Link(){};

        bool setup() override;
        void loop() override;

    private:
        static const size_t TEXT_MAX = 128;

        // settable_handler_t: bool (*)(void*, const char*). A watch on another device's
        // value topic, not a settable property of this device, so there is nothing to
        // refuse — it returns true even for a retraction, which is deliberately ignored
        // so the source keeps its last good value.
        static bool on_remote_value(void* ctx, const char* value);
        // Bind every * reference to a real device id. Returns true when nothing is left
        // unresolved, so loop() can proceed. Controller builds only.
        bool resolve_wildcards();
        void render(char* out, size_t out_len) const;
        bool deliver(const char* text);

        const char* _source;
        const char* _target;
        const char* _format;
        int         _decimals;
        uint32_t    _interval_ms;

        bool       _ok = false;              // sources/target parsed
        LinkSource _sources[MAX_SOURCES];
        int        _num_sources = 0;
        bool       _has_local   = false;

        bool _target_remote = false;
        char _tgt_dev[HOMIE_DEVICE_ID_MAX + 1]    = {0};   // remote target, by id (controller sends by id)
        char _tgt_node[HOMIE_NODE_ID_MAX + 1]     = {0};
        char _tgt_prop[HOMIE_PROPERTY_ID_MAX + 1] = {0};
        char _target_topic[HOMIE_TOPIC_MAX + 1]   = {0};   // a longer one disables the link
        bool _target_ready = false;          // controller build: target's effective state
        bool _tgt_wild     = false;          // target written as a * pattern, not yet bound
        bool _tgt_refused  = false;          // ... and it matched more than one device
        bool _tgt_unmatched = false;         // ... and matched nothing at the last attempt
        // A wait or an unmatched pattern is logged when it starts, then at most every
        // LINK_REMINDER_MS: these stamp the last line of each.
        bool     _settle_logged     = false;
        uint32_t _settle_logged_ms  = 0;
        uint32_t _unmatched_said_ms = 0;

        bool     _remote_changed = false;    // set by on_remote_value(), cleared by loop()
        char     _sent[TEXT_MAX] = {0};      // last text the target accepted
        bool     _have_sent      = false;
        uint32_t _last_poll_ms   = 0;
        bool     _warned_missing = false;
        bool     _failing        = false;
        uint32_t _failed_at_ms   = 0;        // last failed delivery
        uint32_t _retry_wait_ms  = 0;        // backoff before the next attempt
};
