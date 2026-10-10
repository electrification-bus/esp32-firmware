#include <logic/link.h>
#include <logic/link_core.h>
#include <util/utf8.h>
#include <stdlib.h>
#include <homie/homie.h>
#include <homie/homie_id.h>
#include <homie/Device.h>     // theDevice
#include <homie/Node.h>
#include <homie/Property.h>
#include <platform/mqtt_client.h>
#ifdef ENABLE_CONTROLLER
#include <homie/controller.h>
#endif

// A controller build has no nodes of its own, so there both ends must name another device.
#ifdef ENABLE_CONTROLLER
static const int MIN_PARTS = 3;
static const char* const REF_FORM = "<device-id>/<node>/<property> (controller link)";
#else
static const int MIN_PARTS = 2;
static const char* const REF_FORM = "<node>/<property> or <device-id>/<node>/<property>";
#endif

#ifdef ENABLE_CONTROLLER
// ── binding * references to real devices ──────────────────────────────────────────────
// Discovery is asynchronous: devices announce themselves with $state, and their node and
// property structure only arrives with $description. A pattern can therefore only be
// matched honestly once EVERY discovered device has described itself — bind earlier and
// "the one device with this property" means "the one that happened to answer first",
// which is the broker's retained-delivery order and differs between boots.
//
// Nothing times this out. A device whose retained $state is on the broker with no
// $description behind it is stale state that wants clearing, and a link refusing to bind
// is the right signal; binding against a half-discovered fleet is the failure this exists
// to prevent. Live devices republish their description on reconnect
// (on_mqtt_reconnect -> forget_description_hashes + republish_tree) and a description the
// inbox had to drop is re-subscribed a few seconds later, so both ordinary causes heal.
static bool discovery_settled(char* waiting_on, size_t waiting_len, int* missing, int* total) {
    int n = controller_device_count();
    *total = n;
    *missing = 0;
    if (waiting_len) waiting_on[0] = '\0';
    for (int i = 0; i < n; i++) {
        const ControllerDevice* cd = controller_device_at(i);
        if (!cd || cd->has_description) continue;
        (*missing)++;
        const char* id = cd->device ? cd->device->getId() : "?";
        size_t used = strlen(waiting_on);
        snprintf(waiting_on + used, waiting_len - used, "%s%s", used ? ", " : "", id);
    }
    return n > 0 && *missing == 0;
}

// Every discovered device/node pair matching `dev_pat`/`node_pat` that has `prop` (and,
// with `want_settable`, for a target, has it settable, so a pattern cannot bind a target to
// a read-only property) is added to `out`.
static void resolve_ref(const char* dev_pat, const char* node_pat, const char* prop,
                        bool want_settable, LinkResolve* out) {
    int n = controller_device_count();
    for (int i = 0; i < n; i++) {
        const ControllerDevice* cd = controller_device_at(i);
        Device* d = cd ? cd->device : nullptr;
        if (!d) continue;
        for (int j = 0; j < d->numNodes(); j++) {
            Node* nd = d->nodeAt(j);
            if (!nd || !link_ref_matches(dev_pat, node_pat, d->getId(), nd->id())) continue;
            Property* pr = controller_get_property(d->getId(), nd->id(), prop);
            if (!pr) continue;
            if (want_settable && !pr->settable()) continue;
            // Every matching node counts: two on one device are as ambiguous as two devices.
            link_resolve_add(out, d->getId(), nd->id());
        }
    }
}

bool Link::resolve_wildcards() {
    bool pending = _tgt_wild;
    for (int i = 0; i < _num_sources; i++) if (_sources[i].wild) pending = true;
    if (!pending) return true;

    char waiting[160]; int missing = 0, total = 0;
    if (!discovery_settled(waiting, sizeof(waiting), &missing, &total)) {
        if (!_settle_logged || millis() - _settle_logged_ms >= LINK_REMINDER_MS) {
            _settle_logged = true;
            _settle_logged_ms = millis();
            if (total == 0) {
                Serial.printf("LINK[%s]: waiting for discovery to settle — no devices yet\n",
                              getNodeId());
            } else {
                Serial.printf("LINK[%s]: waiting for discovery to settle — %d of %d device(s) "
                              "without $description (%s)\n", getNodeId(), missing, total, waiting);
            }
        }
        return false;
    }

    // An unmatched pattern is retried every interval but said only when it starts, and
    // then as a reminder.
    bool remind = (millis() - _unmatched_said_ms >= LINK_REMINDER_MS);
    bool said = false;
    for (int i = 0; i < _num_sources; i++) {
        LinkSource& sc = _sources[i];
        if (!sc.wild || sc.refused) continue;
        LinkResolve r;
        resolve_ref(sc.dev, sc.node, sc.prop, false, &r);
        LinkResolveResult res = link_resolve_result(&r);
        if (res == LINK_RESOLVE_MANY) {
            sc.refused = true;
            Serial.printf("LINK[%s]: source %%%d '%s/%s/%s' matches more than one node (%s) — "
                          "not bound; name the device, or narrow the node\n",
                          getNodeId(), i + 1, sc.dev, sc.node, sc.prop, r.cands);
        } else if (res == LINK_RESOLVE_ONE) {
            snprintf(sc.dev,  sizeof(sc.dev),  "%s", r.dev);
            snprintf(sc.node, sizeof(sc.node), "%s", r.node);
            sc.wild = false;
            Serial.printf("LINK[%s]: source %%%d bound to %s/%s/%s\n",
                          getNodeId(), i + 1, sc.dev, sc.node, sc.prop);
        } else if (!sc.unmatched || remind) {
            Serial.printf("LINK[%s]: source %%%d '%s/%s/%s' matched no discovered device; "
                          "retrying\n", getNodeId(), i + 1, sc.dev, sc.node, sc.prop);
            said = true;
        }
        sc.unmatched = (res == LINK_RESOLVE_NONE);
    }

    if (_tgt_wild && !_tgt_refused) {
        LinkResolve r;
        resolve_ref(_tgt_dev, _tgt_node, _tgt_prop, true, &r);
        LinkResolveResult res = link_resolve_result(&r);
        if (res == LINK_RESOLVE_MANY) {
            _tgt_refused = true;
            Serial.printf("LINK[%s]: target '%s/%s/%s' matches more than one node (%s) — "
                          "not bound; name the device, or narrow the node\n",
                          getNodeId(), _tgt_dev, _tgt_node, _tgt_prop, r.cands);
        } else if (res == LINK_RESOLVE_ONE) {
            snprintf(_tgt_dev,  sizeof(_tgt_dev),  "%s", r.dev);
            snprintf(_tgt_node, sizeof(_tgt_node), "%s", r.node);
            int n = snprintf(_target_topic, sizeof(_target_topic), "%s/%s/%s/%s/set",
                             HOMIE_TOPIC_PREFIX, _tgt_dev, _tgt_node, _tgt_prop);
            if (n < 0 || n >= (int)sizeof(_target_topic)) {
                _tgt_refused = true;
                Serial.printf("LINK[%s]: target bound to %s/%s, but its /set topic is over "
                              "%d chars; not bound\n", getNodeId(), _tgt_dev, _tgt_node,
                              HOMIE_TOPIC_MAX);
                return false;
            }
            _tgt_wild = false;
            Serial.printf("LINK[%s]: target bound to %s -> %s\n",
                          getNodeId(), _tgt_dev, _target_topic);
        } else if (!_tgt_unmatched || remind) {
            Serial.printf("LINK[%s]: target '%s/%s/%s' matched no discovered settable "
                          "property; retrying\n", getNodeId(), _tgt_dev, _tgt_node, _tgt_prop);
            said = true;
        }
        _tgt_unmatched = (res == LINK_RESOLVE_NONE);
    }
    if (said) _unmatched_said_ms = millis();

    if (_tgt_wild || _tgt_refused) return false;
    for (int i = 0; i < _num_sources; i++) if (_sources[i].wild || _sources[i].refused) return false;
    return true;
}
#endif  // ENABLE_CONTROLLER

bool Link::setup() {
#ifndef ENABLE_CONTROLLER
    NodeEntity::setup();   // a controller build has no device to make the setup context
#endif

    // ---- target ----
    LinkRef tgt;
    LinkRefStatus st = link_parse_ref(_target, strlen(_target), &tgt);
    if (st != LINK_REF_OK || tgt.parts < MIN_PARTS) {
        Serial.printf("LINK[%s]: bad target '%s': %s; expected %s; link disabled\n",
                      getNodeId(), _target,
                      st != LINK_REF_OK ? link_ref_status_text(st) : "too few parts", REF_FORM);
        return false;
    }
    _target_remote = (tgt.parts == 3);
    int topic_len = 0;
    if (_target_remote) {
        snprintf(_tgt_dev,  sizeof(_tgt_dev),  "%s", tgt.dev);
        snprintf(_tgt_node, sizeof(_tgt_node), "%s", tgt.node);
        snprintf(_tgt_prop, sizeof(_tgt_prop), "%s", tgt.prop);
        _tgt_wild = tgt.wild;
#ifndef ENABLE_CONTROLLER
        if (_tgt_wild) {
            Serial.printf("LINK[%s]: target '%s' uses *, which needs the controller's "
                          "discovery — only a controller build resolves it; link disabled\n",
                          getNodeId(), _target);
            return false;
        }
#endif
        // The topic needs the real device id, so a pattern builds it at bind time.
        if (!_tgt_wild) {
            topic_len = snprintf(_target_topic, sizeof(_target_topic), "%s/%s/%s/%s/set",
                                 HOMIE_TOPIC_PREFIX, tgt.dev, tgt.node, tgt.prop);
        }
    } else {
        // theDevice.topic() is "<prefix>/<device-id>/", with the trailing slash.
        topic_len = snprintf(_target_topic, sizeof(_target_topic), "%s%s/%s/set",
                             theDevice.topic(), tgt.node, tgt.prop);
    }
    if (topic_len < 0 || topic_len >= (int)sizeof(_target_topic)) {
        Serial.printf("LINK[%s]: target topic for '%s' is over %d chars; link disabled\n",
                      getNodeId(), _target, HOMIE_TOPIC_MAX);
        return false;
    }

    // ---- sources: comma-separated ----
    const char* p = _source;
    while (true) {
        size_t len = strcspn(p, ",");
        LinkRef src;
        st = link_parse_ref(p, len, &src);
        if (st != LINK_REF_OK || src.parts < MIN_PARTS || _num_sources == MAX_SOURCES) {
            Serial.printf("LINK[%s]: bad source list '%s': %s; expected up to %d "
                          "comma-separated %s; link disabled\n", getNodeId(), _source,
                          st != LINK_REF_OK ? link_ref_status_text(st)
                          : (_num_sources == MAX_SOURCES ? "too many sources" : "too few parts"),
                          MAX_SOURCES, REF_FORM);
            return false;
        }
        LinkSource& s = _sources[_num_sources++];
        s.owner  = this;
        s.remote = (src.parts == 3);
        if (s.remote) {
            snprintf(s.dev,  sizeof(s.dev),  "%s", src.dev);
            snprintf(s.node, sizeof(s.node), "%s", src.node);
            snprintf(s.prop, sizeof(s.prop), "%s", src.prop);
            s.wild = src.wild;
#ifdef ENABLE_CONTROLLER
            // The controller already subscribes <device>/+/+ for every device it discovers
            // and keeps the values; a second, exact subscription could make some brokers
            // deliver each message twice. Read its cache instead.
            if (s.wild) {
                Serial.printf("LINK[%s]: source %%%d = %s/%s/%s — pattern, binds once "
                              "discovery settles\n", getNodeId(), _num_sources,
                              s.dev, s.node, s.prop);
            } else {
                Serial.printf("LINK[%s]: source %%%d = %s/%s/%s (controller cache)\n",
                              getNodeId(), _num_sources, s.dev, s.node, s.prop);
            }
#else
            if (s.wild) {
                Serial.printf("LINK[%s]: source %%%d uses *, which needs the controller's "
                              "discovery — only a controller build resolves it; link "
                              "disabled\n", getNodeId(), _num_sources);
                return false;
            }
            char topic[HOMIE_TOPIC_MAX + 1];
            int n = snprintf(topic, sizeof(topic), "%s/%s/%s/%s", HOMIE_TOPIC_PREFIX,
                             s.dev, s.node, s.prop);
            if (n < 0 || n >= (int)sizeof(topic)) {
                Serial.printf("LINK[%s]: source %%%d topic is over %d chars; link disabled\n",
                              getNodeId(), _num_sources, HOMIE_TOPIC_MAX);
                return false;
            }
            // Same mechanism as main.cpp's $state watch: the handler table copies the topic
            // and is re-subscribed wholesale (at QoS 0) on every reconnect, so registering
            // keeps the watch alive; settable_subscribe_now() covers a connection already up,
            // at the same QoS, and does nothing while the transport is down.
            //
            // The table holds ONE handler per topic and a second registration replaces the
            // first, which would leave the earlier source without values for good. So a
            // topic that is already registered (another link's source, or this link's own
            // earlier one) is refused here.
            if (settable_is_registered(topic)) {
                Serial.printf("LINK[%s]: source %%%d %s is already watched by another link "
                              "source; one topic can feed only one link source; link "
                              "disabled\n", getNodeId(), _num_sources, topic);
                return false;
            }
            subscribe_settable_handler(topic, &Link::on_remote_value, &s);
            if (!settable_is_registered(topic)) {
                // The table was full (the core logged it). Without the entry no value
                // ever arrives, so the link could never send.
                Serial.printf("LINK[%s]: source %%%d %s could not be registered; link "
                              "disabled\n", getNodeId(), _num_sources, topic);
                return false;
            }
            settable_subscribe_now(topic);
            Serial.printf("LINK[%s]: source %%%d = %s (remote)\n", getNodeId(), _num_sources, topic);
#endif
        } else {
            snprintf(s.node, sizeof(s.node), "%s", src.node);
            snprintf(s.prop, sizeof(s.prop), "%s", src.prop);
            _has_local = true;
            Serial.printf("LINK[%s]: source %%%d = %s/%s (local, every %u ms)\n", getNodeId(),
                          _num_sources, s.node, s.prop, (unsigned)_interval_ms);
        }
        p += len;
        if (*p == '\0') break;
        p++;   // past the comma
    }

    _ok = true;
    Serial.printf("LINK[%s]: -> %s\n", getNodeId(), _target_topic);
    return true;
}

// Called by settable_dispatch() from mqtt_process_queue() on the main task, not from the
// receive callback. It only stores; loop() renders and delivers. An empty payload is a
// retraction: keep the last good value instead.
bool Link::on_remote_value(void* ctx, const char* value) {
    LinkSource* s = (LinkSource*)ctx;
    // A retraction (empty payload) is not an error and not a new reading: keep the last
    // good value rather than blanking the target. true either way — see the declaration.
    if (!link_take_value(s->value, sizeof(s->value), value, true)) return true;
    s->has_value = true;
    s->owner->_remote_changed = true;
    return true;
}

void Link::render(char* out, size_t out_len) const {
    const char* values[MAX_SOURCES];
    for (int i = 0; i < _num_sources; i++) values[i] = _sources[i].value;
    link_render(_format, values, _num_sources, _decimals, out, out_len);
}

bool Link::deliver(const char* text) {
#ifdef ENABLE_CONTROLLER
    // Counted in the controller's stats; non-retained, QoS 0 (Homie).
    return controller_set_property(_tgt_dev, _tgt_node, _tgt_prop, text);
#else
    if (!_target_remote) return deliver_local_set(_target_topic, text);
    // /set is non-retained, QoS 0 (Homie). Through the transport, so a link that is not
    // up (or is reconnecting) drops it and returns false instead of writing to the client.
    return mqtt_transport.publish(_target_topic, text, false, 0);
#endif
}

// Copy a source value in, keeping the last good one if there is none (no reading yet, or
// retracted). A property value can be 255 bytes; a link source keeps at most the first
// 63, cut on a UTF-8 character boundary.
static void take_value(LinkSource& s, Property* prop) {
    // A controller's copy keeps the last text in value() after a retraction and clears
    // has_value(); a local property's has_value() is false until its first reading.
    if (link_take_value(s.value, sizeof(s.value), prop->value(), prop->has_value())) {
        s.has_value = true;
    }
}

void Link::loop() {
    if (!_ok) return;

#ifdef ENABLE_CONTROLLER
    // Controller build: everything comes from the discovery cache, so poll it on the
    // interval. The target's EFFECTIVE state (Homie 5 root rule — a child behind a lost
    // root is lost) gates delivery: /set is non-retained, so a command sent to a device
    // that is not there is simply gone. Each time the target comes back to ready, the
    // current text is sent again — a display that rebooted or reconnected catches up
    // instead of staying blank or stale until the reading next changes.
    if (millis() - _last_poll_ms < _interval_ms) return;
    _last_poll_ms = millis();

    // Every id below must be a real one. A pattern that has not bound yet — or that
    // refused because it was ambiguous — leaves this link idle rather than guessing.
    if (!resolve_wildcards()) return;

    bool ready = (controller_effective_state(_tgt_dev) == DeviceState::DEVICE_STATE_READY);
    if (ready && !_target_ready) {
        Serial.printf("LINK[%s]: target %s is ready — sending current text\n", getNodeId(), _tgt_dev);
        _have_sent = false;
        _failing = false;   // a target that came back is tried at once, not after a backoff
    }
    _target_ready = ready;

    for (int i = 0; i < _num_sources; i++) {
        LinkSource& s = _sources[i];
        Property* prop = controller_get_property(s.dev, s.node, s.prop);
        if (prop) take_value(s, prop);
    }
    if (!ready) return;
#else
    // Remote values are handled as soon as they arrive; the interval paces local polling
    // and retries of a failed delivery.
    bool due = (millis() - _last_poll_ms >= _interval_ms);
    if (!_remote_changed && !due) return;
    _remote_changed = false;
    if (due) _last_poll_ms = millis();

    if (due && _has_local) {
        for (int i = 0; i < _num_sources; i++) {
            LinkSource& s = _sources[i];
            if (s.remote) continue;
            Node* node = theDevice.getNode(s.node);
            Property* prop = node ? node->getProperty(s.prop) : nullptr;
            if (!prop) {
                if (!_warned_missing) {
                    Serial.printf("LINK[%s]: source %s/%s not found on this device\n",
                                  getNodeId(), s.node, s.prop);
                    _warned_missing = true;
                }
                continue;
            }
            take_value(s, prop);
        }
    }
#endif

    for (int i = 0; i < _num_sources; i++) {
        if (!_sources[i].has_value) return;   // wait until every placeholder can be filled
    }

    char text[TEXT_MAX];
    render(text, sizeof(text));
    if (_have_sent && strcmp(text, _sent) == 0) return;
    // Backing off after a failure: each attempt can log in the core ("NOT DELIVERED",
    // "not settable"), so a new value does not shorten the wait either.
    if (_failing && millis() - _failed_at_ms < _retry_wait_ms) return;

    if (deliver(text)) {
        snprintf(_sent, sizeof(_sent), "%s", text);
        _have_sent = true;
        if (_failing) Serial.printf("LINK[%s]: delivered to %s\n", getNodeId(), _target_topic);
        _failing = false;
        _retry_wait_ms = 0;
        return;
    }
    if (!_failing) {
        // Logged once per run of failures, not on every retry.
        Serial.printf("LINK[%s]: delivery to %s failed; retrying, backing off from %u ms to "
                      "%u ms\n", getNodeId(), _target_topic, (unsigned)_interval_ms,
                      (unsigned)LINK_RETRY_MAX_MS);
        _failing = true;
        _retry_wait_ms = 0;
    }
    _retry_wait_ms = link_next_retry_ms(_retry_wait_ms, _interval_ms);
    _failed_at_ms = millis();
}
